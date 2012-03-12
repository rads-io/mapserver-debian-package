/******************************************************************************
 * $Id: mapgeos.c 11470 2011-04-05 20:11:33Z tamas $
 *
 * Project:  MapServer
 * Purpose:  MapServer-GEOS integration.
 * Author:   Steve Lime and the MapServer team.
 *
 ******************************************************************************
 * Copyright (c) 1996-2005 Regents of the University of Minnesota.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "mapserver.h"

#ifdef USE_GEOS

#include <geos_c.h>

/*
** Error handling...
*/
static void msGEOSError(const char *format, ...)
{
  va_list args;

  va_start (args, format);
  msSetError(MS_GEOSERR, format, "msGEOSError()", args); /* just pass along to MapServer error handling */
  va_end(args);

  return;
}

static void msGEOSNotice(const char *fmt, ...) 
{
  return; /* do nothing with notices at this point */
}

/*
** Setup/Cleanup wrappers
*/
void msGEOSSetup() 
{
  initGEOS(msGEOSNotice, msGEOSError);
}

void msGEOSCleanup() 
{
  finishGEOS();
}

/*
** Translation functions
*/
static GEOSGeom msGEOSShape2Geometry_point(pointObj *point)
{
  GEOSCoordSeq coords;
  GEOSGeom g;
  
  if(!point) return NULL;
  
  coords = GEOSCoordSeq_create(1, 2); /* todo handle z's */
  if(!coords) return NULL;
  
  GEOSCoordSeq_setX(coords, 0, point->x);
  GEOSCoordSeq_setY(coords, 0, point->y);
  /* GEOSCoordSeq_setY(coords, 0, point->z); */

  g = GEOSGeom_createPoint(coords); /* g owns the coordinate in coords */
  
  return g;
}

static GEOSGeom msGEOSShape2Geometry_multipoint(lineObj *multipoint)
{
  int i;
  GEOSGeom g;
  GEOSGeom *points;

  if(!multipoint) return NULL;

  points = malloc(multipoint->numpoints*sizeof(GEOSGeom));
  if(!points) return NULL;
  
  for(i=0; i<multipoint->numpoints; i++)
    points[i] = msGEOSShape2Geometry_point(&(multipoint->point[i]));

  g = GEOSGeom_createCollection(GEOS_MULTIPOINT, points, multipoint->numpoints);
  
  free(points);

  return g;
}

static GEOSGeom msGEOSShape2Geometry_line(lineObj *line)
{
  int i;
  GEOSGeom g;
  GEOSCoordSeq coords;  

  if(!line) return NULL;
  
  coords = GEOSCoordSeq_create(line->numpoints, 2); /* todo handle z's */
  if(!coords) return NULL;

  for(i=0; i<line->numpoints; i++) {
    GEOSCoordSeq_setX(coords, i, line->point[i].x);
    GEOSCoordSeq_setY(coords, i, line->point[i].y);
    /* GEOSCoordSeq_setZ(coords, i, line->point[i].z); */
  }

  g = GEOSGeom_createLineString(coords); /* g owns the coordinates in coords */
  
  return g;
}

static GEOSGeom msGEOSShape2Geometry_multiline(shapeObj *multiline)
{
  int i;
  GEOSGeom g;
  GEOSGeom *lines;

  if(!multiline) return NULL;

  lines = malloc(multiline->numlines*sizeof(GEOSGeom));
  if(!lines) return NULL;

  for(i=0; i<multiline->numlines; i++)
    lines[i] = msGEOSShape2Geometry_line(&(multiline->line[i]));

  g = GEOSGeom_createCollection(GEOS_MULTILINESTRING, lines, multiline->numlines);
  
  free(lines);

  return g;
}

static GEOSGeom msGEOSShape2Geometry_simplepolygon(shapeObj *shape, int r, int *outerList)
{
  int i, j, k;
  GEOSCoordSeq coords; 
  GEOSGeom g;
  GEOSGeom outerRing;
  GEOSGeom *innerRings=NULL;
  int numInnerRings=0, *innerList;

  if(!shape || !outerList) return NULL;
    
  /* build the outer shell */
  coords = GEOSCoordSeq_create(shape->line[r].numpoints, 2); /* todo handle z's */
  if(!coords) return NULL;
    
  for(i=0; i<shape->line[r].numpoints; i++) {
    GEOSCoordSeq_setX(coords, i, shape->line[r].point[i].x);
    GEOSCoordSeq_setY(coords, i, shape->line[r].point[i].y);
    /* GEOSCoordSeq_setZ(coords, i, shape->line[r].point[i].z); */
  }
  
  outerRing = GEOSGeom_createLinearRing(coords); /* outerRing owns the coordinates in coords */ 
    
  /* build the holes */
  innerList = msGetInnerList(shape, r, outerList);    
  for(j=0; j<shape->numlines; j++)
    if(innerList[j] == MS_TRUE) numInnerRings++;

  if(numInnerRings > 0) {
    k = 0; /* inner ring counter */

    innerRings = malloc(numInnerRings*sizeof(GEOSGeom));
    if(!innerRings) return NULL; /* todo, this will leak memory (outerRing) */
  
    for(j=0; j<shape->numlines; j++) {
	  if(innerList[j] == MS_FALSE) continue;
	
	  coords = GEOSCoordSeq_create(shape->line[j].numpoints, 2); /* todo handle z's */
      if(!coords) return NULL; /* todo, this will leak memory (shell + allocated holes) */
	
	  for(i=0; i<shape->line[j].numpoints; i++) {
        GEOSCoordSeq_setX(coords, i, shape->line[j].point[i].x);
        GEOSCoordSeq_setY(coords, i, shape->line[j].point[i].y);
        /* GEOSCoordSeq_setZ(coords, i, shape->line[j].point[i].z); */
	  }

	  innerRings[k] = GEOSGeom_createLinearRing(coords); /* innerRings[k] owns the coordinates in coords */
	  k++;
    }
  }

  g = GEOSGeom_createPolygon(outerRing, innerRings, numInnerRings);

  free(innerList); /* clean up */
   
  return g;
}

static GEOSGeom msGEOSShape2Geometry_polygon(shapeObj *shape)
{
  int i, j;
  GEOSGeom *polygons;
  int *outerList, numOuterRings=0, lastOuterRing=0;
  GEOSGeom g;

  outerList = msGetOuterList(shape);
  for(i=0; i<shape->numlines; i++) {
    if(outerList[i] == MS_TRUE) {
	  numOuterRings++;
	  lastOuterRing = i; /* save for the simple case */
    }
  }

  if(numOuterRings == 1) { 
    g = msGEOSShape2Geometry_simplepolygon(shape, lastOuterRing, outerList);
  } else { /* a true multipolygon */
    polygons = malloc(numOuterRings*sizeof(GEOSGeom));
    if(!polygons) return NULL;

    j = 0; /* part counter */
    for(i=0; i<shape->numlines; i++) {
	  if(outerList[i] == MS_FALSE) continue;
	  polygons[j] = msGEOSShape2Geometry_simplepolygon(shape, i, outerList); /* TODO: account for NULL return values */
	  j++;
    }

    g = GEOSGeom_createCollection(GEOS_MULTIPOLYGON, polygons, numOuterRings);
  }

  free(outerList);
  return g;
}

GEOSGeom msGEOSShape2Geometry(shapeObj *shape)
{
  if(!shape)
    return NULL; /* a NULL shape generates a NULL geometry */

  switch(shape->type) {
  case MS_SHAPE_POINT:
    if(shape->numlines == 0 || shape->line[0].numpoints == 0) /* not enough info for a point */
      return NULL;

    if(shape->line[0].numpoints == 1) /* simple point */
      return msGEOSShape2Geometry_point(&(shape->line[0].point[0]));
    else /* multi-point */
      return msGEOSShape2Geometry_multipoint(&(shape->line[0]));
    break;
  case MS_SHAPE_LINE:
    if(shape->numlines == 0 || shape->line[0].numpoints < 2) /* not enough info for a line */
      return NULL;

    if(shape->numlines == 1) /* simple line */
      return msGEOSShape2Geometry_line(&(shape->line[0]));
    else /* multi-line */
      return msGEOSShape2Geometry_multiline(shape);
    break;
  case MS_SHAPE_POLYGON:
    if(shape->numlines == 0 || shape->line[0].numpoints < 4) /* not enough info for a polygon (first=last) */
      return NULL;

    return msGEOSShape2Geometry_polygon(shape); /* simple and multipolygon cases are addressed */
    break;
  default:
    break;
  }

  return NULL; /* should not get here */
}

static shapeObj *msGEOSGeometry2Shape_point(GEOSGeom g)
{
  GEOSCoordSeq coords;
  shapeObj *shape=NULL;
  
  if(!g) return NULL;
    
  shape = (shapeObj *) malloc(sizeof(shapeObj));
  msInitShape(shape);
    
  shape->type = MS_SHAPE_POINT;
  shape->line = (lineObj *) malloc(sizeof(lineObj));
  shape->numlines = 1;
  shape->line[0].point = (pointObj *) malloc(sizeof(pointObj));
  shape->line[0].numpoints = 1;
  shape->geometry = (GEOSGeom) g;

  coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(g);

  GEOSCoordSeq_getX(coords, 0, &(shape->line[0].point[0].x));
  GEOSCoordSeq_getY(coords, 0, &(shape->line[0].point[0].y));
  /* GEOSCoordSeq_getZ(coords, 0, &(shape->line[0].point[0].z)); */

  shape->bounds.minx = shape->bounds.maxx = shape->line[0].point[0].x;
  shape->bounds.miny = shape->bounds.maxy = shape->line[0].point[0].y;
 
  return shape;
}

static shapeObj *msGEOSGeometry2Shape_multipoint(GEOSGeom g)
{
  int i;
  int numPoints;
  GEOSCoordSeq coords;
  GEOSGeom point;
  
  shapeObj *shape=NULL;

  if(!g) return NULL;
  numPoints = GEOSGetNumGeometries(g); /* each geometry has 1 point */

  shape = (shapeObj *) malloc(sizeof(shapeObj));
  msInitShape(shape);

  shape->type = MS_SHAPE_POINT;
  shape->line = (lineObj *) malloc(sizeof(lineObj));
  shape->numlines = 1;
  shape->line[0].point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
  shape->line[0].numpoints = numPoints;
  shape->geometry = (GEOSGeom) g;

  for(i=0; i<numPoints; i++) {
    point = (GEOSGeom) GEOSGetGeometryN(g, i);
    coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(point);

    GEOSCoordSeq_getX(coords, 0, &(shape->line[0].point[i].x));
    GEOSCoordSeq_getY(coords, 0, &(shape->line[0].point[i].y));
    /* GEOSCoordSeq_getZ(coords, 0, &(shape->line[0].point[i].z)); */
  }
 
  msComputeBounds(shape); 

  return shape;
}

static shapeObj *msGEOSGeometry2Shape_line(GEOSGeom g)
{
  shapeObj *shape=NULL;

  int i;
  int numPoints;
  GEOSCoordSeq coords;

  if(!g) return NULL;
  numPoints = GEOSGetNumCoordinates(g);
  coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(g);

  shape = (shapeObj *) malloc(sizeof(shapeObj));
  msInitShape(shape);

  shape->type = MS_SHAPE_LINE; 
  shape->line = (lineObj *) malloc(sizeof(lineObj));
  shape->numlines = 1;
  shape->line[0].point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
  shape->line[0].numpoints = numPoints;
  shape->geometry = (GEOSGeom) g;

  for(i=0; i<numPoints; i++) {
    GEOSCoordSeq_getX(coords, i, &(shape->line[0].point[i].x));
    GEOSCoordSeq_getY(coords, i, &(shape->line[0].point[i].y));
    /* GEOSCoordSeq_getZ(coords, i, &(shape->line[0].point[i].z)); */
  }

  msComputeBounds(shape); 

  return shape;
}

static shapeObj *msGEOSGeometry2Shape_multiline(GEOSGeom g)
{
  int i, j;
  int numPoints, numLines;
  GEOSCoordSeq coords;
  GEOSGeom lineString;

  shapeObj *shape=NULL;
  lineObj line;

  if(!g) return NULL;
  numLines = GEOSGetNumGeometries(g);

  shape = (shapeObj *) malloc(sizeof(shapeObj));
  msInitShape(shape);

  shape->type = MS_SHAPE_LINE;
  shape->geometry = (GEOSGeom) g;

  for(j=0; j<numLines; j++) {
    lineString = (GEOSGeom) GEOSGetGeometryN(g, j);
    numPoints = GEOSGetNumCoordinates(lineString);
    coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(lineString);

    line.point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
    line.numpoints = numPoints;

    for(i=0; i<numPoints; i++) {
      GEOSCoordSeq_getX(coords, i, &(line.point[i].x));
      GEOSCoordSeq_getY(coords, i, &(line.point[i].y));
      /* GEOSCoordSeq_getZ(coords, i, &(line.point[i].z)); */  	
    }

    msAddLineDirectly(shape, &line);
  }

  msComputeBounds(shape); 

  return shape;
}

static shapeObj *msGEOSGeometry2Shape_polygon(GEOSGeom g)
{
  shapeObj *shape=NULL;
  lineObj line;
  int numPoints, numRings;
  int i, j;

  GEOSCoordSeq coords;
  GEOSGeom ring;

  if(!g) return NULL;

  shape = (shapeObj *) malloc(sizeof(shapeObj));
  msInitShape(shape);
  shape->type = MS_SHAPE_POLYGON;
  shape->geometry = (GEOSGeom) g;

  /* exterior ring */
  ring = (GEOSGeom) GEOSGetExteriorRing(g);
  numPoints = GEOSGetNumCoordinates(ring);
  coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(ring);
  
  line.point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
  line.numpoints = numPoints;

  for(i=0; i<numPoints; i++) {
    GEOSCoordSeq_getX(coords, i, &(line.point[i].x));
    GEOSCoordSeq_getY(coords, i, &(line.point[i].y));
    /* GEOSCoordSeq_getZ(coords, i, &(line.point[i].z)); */    
  }
  msAddLineDirectly(shape, &line);

  /* interior rings */
  numRings = GEOSGetNumInteriorRings(g);
  for(j=0; j<numRings; j++) {
    ring = (GEOSGeom) GEOSGetInteriorRingN(g, j);
    if(GEOSisRing(ring) != 1) continue; /* skip it */

    numPoints = GEOSGetNumCoordinates(ring);
    coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(ring);

    line.point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
    line.numpoints = numPoints;

    for(i=0; i<numPoints; i++) {
      GEOSCoordSeq_getX(coords, i, &(line.point[i].x));
      GEOSCoordSeq_getY(coords, i, &(line.point[i].y));
      /* GEOSCoordSeq_getZ(coords, i, &(line.point[i].z)); */
    }
    msAddLineDirectly(shape, &line);
  }

  msComputeBounds(shape); 

  return shape;
}

static shapeObj *msGEOSGeometry2Shape_multipolygon(GEOSGeom g)
{
  int i, j, k;
  shapeObj *shape=NULL;
  lineObj line;
  int numPoints, numRings, numPolygons;

  GEOSCoordSeq coords;
  GEOSGeom polygon, ring;

  if(!g) return NULL;
  numPolygons = GEOSGetNumGeometries(g);

  shape = (shapeObj *) malloc(sizeof(shapeObj));
  msInitShape(shape);
  shape->type = MS_SHAPE_POLYGON;
  shape->geometry = (GEOSGeom) g;

  for(k=0; k<numPolygons; k++) { /* for each polygon */
    polygon = (GEOSGeom) GEOSGetGeometryN(g, k);

    /* exterior ring */
    ring = (GEOSGeom) GEOSGetExteriorRing(polygon);
    numPoints = GEOSGetNumCoordinates(ring);
    coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(ring);

    line.point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
    line.numpoints = numPoints;

    for(i=0; i<numPoints; i++) {
	  GEOSCoordSeq_getX(coords, i, &(line.point[i].x));
      GEOSCoordSeq_getY(coords, i, &(line.point[i].y));
      /* GEOSCoordSeq_getZ(coords, i, &(line.point[i].z)); */
    }
    msAddLineDirectly(shape, &line);
    
    /* interior rings */
    numRings = GEOSGetNumInteriorRings(polygon);

    for(j=0; j<numRings; j++) {
      ring = (GEOSGeom) GEOSGetInteriorRingN(polygon, j);
      if(GEOSisRing(ring) != 1) continue; /* skip it */      

      numPoints = GEOSGetNumCoordinates(ring);
      coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(ring);	  

      line.point = (pointObj *) malloc(sizeof(pointObj)*numPoints);
      line.numpoints = numPoints;

      for(i=0; i<numPoints; i++) {
	GEOSCoordSeq_getX(coords, i, &(line.point[i].x));
        GEOSCoordSeq_getY(coords, i, &(line.point[i].y));
        /* GEOSCoordSeq_getZ(coords, i, &(line.point[i].z)); */
      }
      msAddLineDirectly(shape, &line);	  
    }
  } /* next polygon */

  msComputeBounds(shape); 

  return shape; 
}

shapeObj *msGEOSGeometry2Shape(GEOSGeom g)
{
  int type;

  if(!g) 
    return NULL; /* a NULL geometry generates a NULL shape */

  type = GEOSGeomTypeId(g);
  switch(type) {
  case GEOS_POINT:
    return msGEOSGeometry2Shape_point(g);
    break;
  case GEOS_MULTIPOINT:
    return msGEOSGeometry2Shape_multipoint(g);
    break;
  case GEOS_LINESTRING:
    return msGEOSGeometry2Shape_line(g);
    break;
  case GEOS_MULTILINESTRING:
    return msGEOSGeometry2Shape_multiline(g);
    break;
  case GEOS_POLYGON:
    return msGEOSGeometry2Shape_polygon(g);
    break;
  case GEOS_MULTIPOLYGON:
    return msGEOSGeometry2Shape_multipolygon(g);
    break;
  default:
    if (!GEOSisEmpty(g))
        msSetError(MS_GEOSERR, "Unsupported GEOS geometry type (%d).", "msGEOSGeometry2Shape()", type);
    return NULL;
  }
}
#endif

/*
** Maintenence functions exposed to MapServer/MapScript.
*/

void msGEOSFreeGeometry(shapeObj *shape)
{
#ifdef USE_GEOS
  GEOSGeom g=NULL;

  if(!shape || !shape->geometry) 
    return;

  g = (GEOSGeom) shape->geometry;
  GEOSGeom_destroy(g);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSFreeGEOSGeom()");
  return;
#endif
}

/*
** WKT input and output functions
*/
shapeObj *msGEOSShapeFromWKT(const char *wkt)
{
#ifdef USE_GEOS
  GEOSGeom g;

  if(!wkt) 
    return NULL;

  g = GEOSGeomFromWKT(wkt);
  if(!g) {
    msSetError(MS_GEOSERR, "Error reading WKT geometry \"%s\".", "msGEOSShapeFromWKT()", wkt);
    return NULL;
  } else {
    return msGEOSGeometry2Shape(g);
  }

#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSShapeFromWKT()");
  return NULL;
#endif
}

/* Return should be freed with msGEOSFreeWKT */
char *msGEOSShapeToWKT(shapeObj *shape)
{
#ifdef USE_GEOS
  GEOSGeom g;

  if(!shape) 
    return NULL;
    
  /* if we have a geometry, we should update it*/
  msGEOSFreeGeometry(shape);

  shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);
  g = (GEOSGeom) shape->geometry;
  if(!g) return NULL;

  return GEOSGeomToWKT(g);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSShapeToWKT()");
  return NULL;
#endif
}

void msGEOSFreeWKT(char* pszGEOSWKT)
{
#ifdef USE_GEOS
#if GEOS_VERSION_MAJOR > 3 || (GEOS_VERSION_MAJOR == 3 && GEOS_VERSION_MINOR >= 2)
  GEOSFree(pszGEOSWKT);
#endif
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSFreeWKT()");
#endif
}

/*
** Analytical functions exposed to MapServer/MapScript.
*/

shapeObj *msGEOSBuffer(shapeObj *shape, double width)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2; 

  if(!shape) 
    return NULL;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);

  g1 = (GEOSGeom) shape->geometry;
  if(!g1) return NULL;
  
  g2 = GEOSBuffer(g1, width, 30);
  return msGEOSGeometry2Shape(g2);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSBuffer()");
  return NULL;
#endif
}

shapeObj *msGEOSSimplify(shapeObj *shape, double tolerance)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2; 

  if(!shape) 
    return NULL;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);

  g1 = (GEOSGeom) shape->geometry;
  if(!g1) return NULL;
  
  g2 = GEOSSimplify(g1, tolerance);
  return msGEOSGeometry2Shape(g2);
#else
  msSetError(MS_GEOSERR, "GEOS Simplify support is not available.", "msGEOSSimplify()");
  return NULL;
#endif
}

shapeObj *msGEOSTopologyPreservingSimplify(shapeObj *shape, double tolerance)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2; 

  if(!shape) 
    return NULL;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);

  g1 = (GEOSGeom) shape->geometry;
  if(!g1) return NULL;
  
  g2 = GEOSTopologyPreserveSimplify(g1, tolerance);
  return msGEOSGeometry2Shape(g2);
#else
  msSetError(MS_GEOSERR, "GEOS Simplify support is not available.", "msGEOSTopologyPreservingSimplify()");
  return NULL;
#endif
}

shapeObj *msGEOSConvexHull(shapeObj *shape)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;

  if(!shape) return NULL;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);
  g1 = (GEOSGeom) shape->geometry;
  if(!g1) return NULL;

  g2 = GEOSConvexHull(g1);
  return msGEOSGeometry2Shape(g2);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSConvexHull()");
  return NULL;
#endif
}

shapeObj *msGEOSBoundary(shapeObj *shape)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;

  if(!shape) return NULL;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);
  g1 = (GEOSGeom) shape->geometry;
  if(!g1) return NULL;

  g2 = GEOSBoundary(g1);
  return msGEOSGeometry2Shape(g2);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSBoundary()");
  return NULL;
#endif
}

pointObj *msGEOSGetCentroid(shapeObj *shape)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  GEOSCoordSeq coords;
  pointObj *point;

  if(!shape) return NULL;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);
  g1 = (GEOSGeom) shape->geometry;
  if(!g1) return NULL;

  g2 = GEOSGetCentroid(g1);

  point = (pointObj *) malloc(sizeof(pointObj));

  coords = (GEOSCoordSeq) GEOSGeom_getCoordSeq(g2);

  GEOSCoordSeq_getX(coords, 0, &(point->x));
  GEOSCoordSeq_getY(coords, 0, &(point->y));
  /* GEOSCoordSeq_getZ(coords, 0, &(point->z)); */

  GEOSCoordSeq_destroy(coords);

  return point;
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSGetCentroid()");
  return NULL;
#endif
}

shapeObj *msGEOSUnion(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2, g3;

  if(!shape1 || !shape2)
    return NULL;

  if(!shape1->geometry) /* if no geometry for the shape then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return NULL;

  if(!shape2->geometry) /* if no geometry for the shape then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return NULL;

  g3 = GEOSUnion(g1, g2);
  return msGEOSGeometry2Shape(g3);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSUnion()");
  return NULL;
#endif
}

shapeObj *msGEOSIntersection(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2, g3;

  if(!shape1 || !shape2)
    return NULL;

  if(!shape1->geometry) /* if no geometry for the shape then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return NULL;

  if(!shape2->geometry) /* if no geometry for the shape then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return NULL;

  g3 = GEOSIntersection(g1, g2);
  return msGEOSGeometry2Shape(g3);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSIntersection()");
  return NULL;
#endif
}

shapeObj *msGEOSDifference(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2, g3;

  if(!shape1 || !shape2)
    return NULL;

  if(!shape1->geometry) /* if no geometry for the shape then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return NULL;

  if(!shape2->geometry) /* if no geometry for the shape then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return NULL;

  g3 = GEOSDifference(g1, g2);
  return msGEOSGeometry2Shape(g3);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSDifference()");
  return NULL;
#endif
}

shapeObj *msGEOSSymDifference(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2, g3;

  if(!shape1 || !shape2)
    return NULL;

  if(!shape1->geometry) /* if no geometry for the shape then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return NULL;

  if(!shape2->geometry) /* if no geometry for the shape then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return NULL;

  g3 = GEOSSymDifference(g1, g2);
  return msGEOSGeometry2Shape(g3);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSSymDifference()");
  return NULL;
#endif
}

/* 
** Binary predicates exposed to MapServer/MapScript
*/

/*
** Does shape1 contain shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSContains(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = shape2->geometry;
  if(!g2) return -1;

  result = GEOSContains(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSContains()");
  return -1;
#endif
}

/*
** Does shape1 overlap shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSOverlaps(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = shape2->geometry;
  if(!g2) return -1;

  result = GEOSOverlaps(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSOverlaps()");
  return -1;
#endif
}

/*
** Is shape1 within shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSWithin(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = shape2->geometry;
  if(!g2) return -1;

  result = GEOSWithin(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSWithin()");
  return -1;
#endif
}

/*
** Does shape1 cross shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSCrosses(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = shape2->geometry;
  if(!g2) return -1;

  result = GEOSCrosses(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSCrosses()");
  return -1;
#endif
}

/*
** Does shape1 intersect shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSIntersects(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return -1;

  result = GEOSIntersects(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  if(!shape1 || !shape2)
    return -1;

  switch(shape1->type) { /* todo: deal with point shapes */
  case(MS_SHAPE_LINE):
    switch(shape2->type) {
    case(MS_SHAPE_LINE):
      return msIntersectPolylines(shape1, shape2);
    case(MS_SHAPE_POLYGON):
      return msIntersectPolylinePolygon(shape1, shape2);
    }
    break;
  case(MS_SHAPE_POLYGON):
    switch(shape2->type) {
    case(MS_SHAPE_LINE):
      return msIntersectPolylinePolygon(shape2, shape1);
    case(MS_SHAPE_POLYGON):
      return msIntersectPolygons(shape1, shape2);
    }
    break;
  }

  return -1;
#endif
}

/*
** Does shape1 touch shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSTouches(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return -1;

  result = GEOSTouches(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSTouches()");
  return -1;
#endif
}

/*
** Does shape1 equal shape2, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSEquals(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return -1;

  result = GEOSEquals(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSEquals()");
  return -1;
#endif
}

/*
** Are shape1 and shape2 disjoint, returns MS_TRUE/MS_FALSE or -1 for an error.
*/
int msGEOSDisjoint(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return -1;

  result = GEOSDisjoint(g1, g2);  
  return ((result==2) ? -1 : result);
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSDisjoint()");
  return -1;
#endif
}

/*
** Useful misc. functions that return -1 on failure.
*/
double msGEOSArea(shapeObj *shape)
{
#if defined(USE_GEOS) && defined(GEOS_CAPI_VERSION_MAJOR) && defined(GEOS_CAPI_VERSION_MINOR) && (GEOS_CAPI_VERSION_MAJOR > 1 || GEOS_CAPI_VERSION_MINOR >= 1)
  GEOSGeom g;
  double area;
  int result;

  if(!shape) return -1;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);
  g = (GEOSGeom) shape->geometry;
  if(!g) return -1;

  result = GEOSArea(g, &area);
  return  ((result==0) ? -1 : area);
#elif defined(USE_GEOS)
  msSetError(MS_GEOSERR, "GEOS support enabled, but old version lacks GEOSArea().", "msGEOSArea()");
  return -1;
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSArea()");
  return -1;
#endif
}

double msGEOSLength(shapeObj *shape)
{
#if defined(USE_GEOS) && defined(GEOS_CAPI_VERSION_MAJOR) && defined(GEOS_CAPI_VERSION_MINOR) && (GEOS_CAPI_VERSION_MAJOR > 1 || GEOS_CAPI_VERSION_MINOR >= 1)

  GEOSGeom g;
  double length;
  int result;

  if(!shape) return -1;

  if(!shape->geometry) /* if no geometry for the shape then build one */
    shape->geometry = (GEOSGeom) msGEOSShape2Geometry(shape);
  g = (GEOSGeom) shape->geometry;
  if(!g) return -1;

  result = GEOSLength(g, &length);
  return  ((result==0) ? -1 : length);
#elif defined(USE_GEOS)
  msSetError(MS_GEOSERR, "GEOS support enabled, but old version lacks GEOSLength().", "msGEOSLength()");
  return -1;
#else
  msSetError(MS_GEOSERR, "GEOS support is not available.", "msGEOSLength()");
  return -1;
#endif
}

double msGEOSDistance(shapeObj *shape1, shapeObj *shape2)
{
#ifdef USE_GEOS
  GEOSGeom g1, g2;
  double distance;
  int result;

  if(!shape1 || !shape2)
    return -1;

  if(!shape1->geometry) /* if no geometry for shape1 then build one */
    shape1->geometry = (GEOSGeom) msGEOSShape2Geometry(shape1);
  g1 = (GEOSGeom) shape1->geometry;
  if(!g1) return -1;

  if(!shape2->geometry) /* if no geometry for shape2 then build one */
    shape2->geometry = (GEOSGeom) msGEOSShape2Geometry(shape2);
  g2 = (GEOSGeom) shape2->geometry;
  if(!g2) return -1;

  result = GEOSDistance(g1, g2, &distance);  
  return ((result==0) ? -1 : distance);
#else
  return msDistanceShapeToShape(shape1, shape2); /* fall back on brute force method (for MapScript) */
#endif
}