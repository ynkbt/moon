/*
 * runtime.cpp: Core surface and canvas definitions.
 *
 * Author:
 *   Miguel de Icaza (miguel@novell.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */
#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include <malloc.h>
#include <glib.h>
#include <stdlib.h>
#include <gdk/gdkx.h>
#if AGG
#    include <agg_rendering_buffer.h>
#    include "Agg2D.h"
#else
#    define CAIRO 1
#endif

#include <cairo-xlib.h>
#include "runtime.h"
#include "shape.h"
#include "transform.h"
#include "animation.h"

#if AGG
struct _SurfacePrivate {
	agg::rendering_buffer rbuf;
	Agg2D *graphics;
};
#endif

NameScope *global_NameScope;

void 
base_ref (Base *base)
{
	if (base->refcount & BASE_FLOATS)
		base->refcount = 1;
	else
		base->refcount++;
}

void
base_unref (Base *base)
{
	if (base->refcount == BASE_FLOATS || base->refcount == 1){
		delete base;
	} else
		base->refcount--;
}

void
Collection::Add (void *data)
{
	list = g_slist_append (list, data);
}

void
Collection::Remove (void *data)
{
	list = g_slist_remove (list, data);
}

void 
collection_add (Collection *collection, void *data)
{
	collection->Add (data);
}

void 
collection_remove (Collection *collection, void *data)
{
	collection->Remove (data);
}

static char**
split_str (const char* s, int *count)
{
	int n;
	// FIXME - what are all the valid separators ? I've seen ',' and ' '
	char** values = g_strsplit_set (s, ", ", 0);
	if (count) {
		// count non-NULL entries (which means we must skip NULLs later too)
		for (n = 0; values[n]; n++);
		*count = n;
	}
	return values;
}

Point
point_from_str (const char *s)
{
	// FIXME - not robust enough for production
	char *next = NULL;
	double x = strtod (s, &next);
	double y = 0.0;
	if (next)
		y = strtod (++next, NULL);
	return Point (x, y);
}

Point*
point_array_from_str (const char *s, int* count)
{
	int i, j, n = 0;
	bool x = true;
	char** values = split_str (s, &n);

	*count = (n >> 1); // 2 doubles for each point
	Point *points = new Point [*count];
	for (i = 0, j = 0; i < n; i++) {
		char *value = values[i];
		if (value) {
			if (x) {
				points[j].x = strtod (value, NULL);
				x = false;
			} else {
				points[j++].y = strtod (value, NULL);
				x = true;
			}
		}
	}

	g_strfreev (values);
	return points;
}

DoubleArray *
double_array_new (int count, double *values)
{
	DoubleArray *p = (DoubleArray *) g_malloc0 (sizeof (DoubleArray) + count * sizeof (double));
	p->basic.count = count;
	p->basic.refcount = 1;
	memcpy (p->values, values, sizeof (double) * count);
	return p;
}

PointArray *
point_array_new (int count, Point *points)
{
	PointArray *p = (PointArray *) g_malloc0 (sizeof (PointArray) + count * sizeof (Point));
	p->basic.count = count;
	p->basic.refcount = 1;
	memcpy (p->points, points, sizeof (Point) * count);
	return p;
};


Rect
rect_from_str (const char *s)
{
	// FIXME - not robust enough for production
	char *next = NULL;
	double x = strtod (s, &next);
	double y = 0.0;
	if (next)
		y = strtod (++next, &next);
	double w = 0.0;
	if (next)
		w = strtod (++next, &next);
	double h = 0.0;
	if (next)
		h = strtod (++next, &next);
	return Rect (x, y, w, h);
}

double*
double_array_from_str (const char *s, int* count)
{
	int i, n;
	char** values = split_str (s, count);

	double *doubles = new double [*count];
	for (i = 0, n = 0; i < *count; i++) {
		char *value = values[i];
		if (value)
			doubles[n++] = strtod (value, NULL);
	}

	g_strfreev (values);
	return doubles;
}

/**
 * Value implementation
 */

void
Value::Init ()
{
	memset (&u, 0, sizeof (u));
}

Value::Value()
  : k (INVALID)
{
	Init ();
}

Value::Value (const Value& v)
{
	k = v.k;
	u = v.u;

	/* make a copy of the string instead of just the pointer */
	if (k == STRING)
		u.s = g_strdup (v.u.s);
	else if (k == POINT_ARRAY)
		u.point_array->basic.refcount++;
	else if (k == DOUBLE_ARRAY)
		u.double_array->basic.refcount++;
}

Value::Value (Kind k)
{
	Init();
	this->k = k;
}

Value::Value(bool z)
{
	Init ();
	k = BOOL;
	u.i32 = z;
}

Value::Value (double d)
{
	Init ();
	k = DOUBLE;
	u.d = d;
}

Value::Value (guint64 i)
{
	Init ();
	k = UINT64;
	u.ui64 = i;
}

Value::Value (gint64 i)
{
	Init ();
	k = INT64;
	u.i64 = i;
}

Value::Value (gint32 i)
{
	Init ();
	k = INT32;
	u.i32 = i;
}

Value::Value (Color c)
{
	Init ();
	k = COLOR;
	u.color = new Color (c);
}

Value
value_color_from_argb (uint32_t c)
{
	return Value (Color (c));
}

Value::Value (DependencyObject *obj)
{
	g_assert (obj != NULL);
	g_assert (obj->GetObjectType () >= Value::DEPENDENCY_OBJECT);
	
	Init ();
	k = obj->GetObjectType ();
	u.dependency_object = obj;
}

Value::Value (Point pt)
{
	Init ();
	k = POINT;
	u.point = new Point (pt);
}

Value::Value (Rect rect)
{
	Init ();
	k = RECT;
	u.rect = new Rect (rect);
}

Value::Value (RepeatBehavior repeat)
{
	Init();
	k = REPEATBEHAVIOR;
	u.repeat = new RepeatBehavior (repeat);
}

Value::Value (Duration duration)
{
	Init();
	k = DURATION;
	u.duration = new Duration (duration);
}

Value::Value (KeyTime keytime)
{
	Init ();
	k = KEYTIME;
	u.keytime = new KeyTime (keytime);
}

Value::Value (const char* s)
{
	Init ();
	k = STRING;
	u.s= g_strdup (s);
}

Value::Value (Point *points, int count)
{
	Init ();
	k = POINT_ARRAY;
	u.point_array = point_array_new (count, points);
}

Value::Value (double *values, int count)
{
	Init ();
	k = DOUBLE_ARRAY;
	u.double_array = double_array_new (count, values);
}

Value::~Value ()
{
	if (k == STRING)
		g_free (u.s);
	else if (k == POINT_ARRAY){
		if (--u.point_array->basic.refcount == 0)
			g_free (u.point_array);
	}
	else if (k == DOUBLE_ARRAY){
		if (--u.double_array->basic.refcount == 0)
			g_free (u.double_array);
	}
}


#define checked_get_exact(kind, errval, mem)  g_return_val_if_fail (k == (kind), errval); return mem;
#define checked_get_subclass(kind, castas)  g_return_val_if_fail (Type::Find((kind))->IsSubclassOf(k) || Type::Find(k)->IsSubclassOf((kind)), NULL); return (castas*)u.dependency_object;
      

#define AS_DEP_SUBCLASS_IMPL(kind, castas) \
castas* Value::As##castas () { checked_get_subclass (kind, castas); }

bool            Value::AsBool () { checked_get_exact (BOOL, false, (bool)u.i32); }
double          Value::AsDouble () { checked_get_exact (DOUBLE, 0.0, u.d); }
guint64         Value::AsUint64 () { checked_get_exact (UINT64, 0, u.ui64); }
gint64          Value::AsInt64 () { checked_get_exact (INT64, 0, u.i64); }
gint32          Value::AsInt32 () { checked_get_exact (INT32, 0, u.i32); }
Color*          Value::AsColor () { checked_get_exact (COLOR, NULL, u.color); }
Point*          Value::AsPoint () { checked_get_exact (POINT, NULL, u.point); }
Rect*           Value::AsRect  () { checked_get_exact (RECT, NULL, u.rect); }
char*           Value::AsString () { checked_get_exact (STRING, NULL, u.s); }
PointArray*     Value::AsPointArray () { checked_get_exact (POINT_ARRAY, NULL, u.point_array); }
DoubleArray*    Value::AsDoubleArray () { checked_get_exact (DOUBLE_ARRAY, NULL, u.double_array); }

RepeatBehavior* Value::AsRepeatBehavior () { checked_get_exact (REPEATBEHAVIOR, NULL, u.repeat); }
Duration*       Value::AsDuration () { checked_get_exact (DURATION, NULL, u.duration); }
KeyTime*        Value::AsKeyTime () { checked_get_exact (KEYTIME, NULL, u.keytime); }

/* nullable primitives (all but bool) */
double*         Value::AsNullableDouble () { checked_get_exact (DOUBLE, NULL, &u.d); }
guint64*        Value::AsNullableUint64 () { checked_get_exact (UINT64, NULL, &u.ui64); }
gint64*         Value::AsNullableInt64 () { checked_get_exact (INT64, NULL, &u.i64); }
gint32*         Value::AsNullableInt32 () { checked_get_exact (INT32, NULL, &u.i32); }


AS_DEP_SUBCLASS_IMPL(DEPENDENCY_OBJECT, DependencyObject)
AS_DEP_SUBCLASS_IMPL(UIELEMENT, UIElement)
AS_DEP_SUBCLASS_IMPL(PANEL, Panel)
AS_DEP_SUBCLASS_IMPL(CANVAS, Canvas)
AS_DEP_SUBCLASS_IMPL(TIMELINE, Timeline)
AS_DEP_SUBCLASS_IMPL(TIMELINEGROUP, TimelineGroup)
AS_DEP_SUBCLASS_IMPL(PARALLELTIMELINE, ParallelTimeline)
AS_DEP_SUBCLASS_IMPL(TRANSFORM, Transform)
AS_DEP_SUBCLASS_IMPL(TRANSFORMGROUP, TransformGroup)
AS_DEP_SUBCLASS_IMPL(ROTATETRANSFORM, RotateTransform)
AS_DEP_SUBCLASS_IMPL(SCALETRANSFORM, ScaleTransform)
AS_DEP_SUBCLASS_IMPL(TRANSLATETRANSFORM, TranslateTransform)
AS_DEP_SUBCLASS_IMPL(MATRIXTRANSFORM, MatrixTransform)
AS_DEP_SUBCLASS_IMPL(STORYBOARD, Storyboard)
AS_DEP_SUBCLASS_IMPL(ANIMATION, Animation)
AS_DEP_SUBCLASS_IMPL(DOUBLEANIMATION, DoubleAnimation)
AS_DEP_SUBCLASS_IMPL(COLORANIMATION, ColorAnimation)
AS_DEP_SUBCLASS_IMPL(POINTANIMATION, PointAnimation)
AS_DEP_SUBCLASS_IMPL(SHAPE, Shape)
AS_DEP_SUBCLASS_IMPL(ELLIPSE, Ellipse)
AS_DEP_SUBCLASS_IMPL(LINE, Line)
AS_DEP_SUBCLASS_IMPL(PATH, Path)
AS_DEP_SUBCLASS_IMPL(POLYGON, Polygon)
AS_DEP_SUBCLASS_IMPL(POLYLINE, Polyline)
AS_DEP_SUBCLASS_IMPL(RECTANGLE, Rectangle)
AS_DEP_SUBCLASS_IMPL(GEOMETRY, Geometry)
AS_DEP_SUBCLASS_IMPL(GEOMETRYGROUP, GeometryGroup)
AS_DEP_SUBCLASS_IMPL(ELLIPSEGEOMETRY, EllipseGeometry)
AS_DEP_SUBCLASS_IMPL(LINEGEOMETRY, LineGeometry)
AS_DEP_SUBCLASS_IMPL(PATHGEOMETRY, PathGeometry)
AS_DEP_SUBCLASS_IMPL(RECTANGLEGEOMETRY, RectangleGeometry)
AS_DEP_SUBCLASS_IMPL(FRAMEWORKELEMENT, FrameworkElement)
AS_DEP_SUBCLASS_IMPL(NAMESCOPE, NameScope)
AS_DEP_SUBCLASS_IMPL(CLOCK, Clock)
AS_DEP_SUBCLASS_IMPL(ANIMATIONCLOCK, AnimationClock)
AS_DEP_SUBCLASS_IMPL(CLOCKGROUP, ClockGroup)
AS_DEP_SUBCLASS_IMPL(BRUSH, Brush)
AS_DEP_SUBCLASS_IMPL(SOLIDCOLORBRUSH, SolidColorBrush)
AS_DEP_SUBCLASS_IMPL(TILEBRUSH, TileBrush)
AS_DEP_SUBCLASS_IMPL(IMAGEBRUSH, ImageBrush)
AS_DEP_SUBCLASS_IMPL(VIDEOBRUSH, VideoBrush)
AS_DEP_SUBCLASS_IMPL(LINEARGRADIENTBRUSH, LinearGradientBrush)
AS_DEP_SUBCLASS_IMPL(GRADIENTBRUSH, GradientBrush)
AS_DEP_SUBCLASS_IMPL(GRADIENTSTOP, GradientStop)
AS_DEP_SUBCLASS_IMPL(PATHFIGURE, PathFigure)
AS_DEP_SUBCLASS_IMPL(PATHSEGMENT, PathSegment)
AS_DEP_SUBCLASS_IMPL(ARCSEGMENT, ArcSegment)
AS_DEP_SUBCLASS_IMPL(BEZIERSEGMENT, BezierSegment)
AS_DEP_SUBCLASS_IMPL(LINESEGMENT, LineSegment)
AS_DEP_SUBCLASS_IMPL(POLYBEZIERSEGMENT, PolyBezierSegment)
AS_DEP_SUBCLASS_IMPL(POLYLINESEGMENT, PolyLineSegment)
AS_DEP_SUBCLASS_IMPL(POLYQUADRATICBEZIERSEGMENT, PolyQuadraticBezierSegment)
AS_DEP_SUBCLASS_IMPL(QUADRATICBEZIERSEGMENT, QuadraticBezierSegment)
AS_DEP_SUBCLASS_IMPL(TRIGGERACTION, TriggerAction)
AS_DEP_SUBCLASS_IMPL(BEGINSTORYBOARD, BeginStoryboard)
AS_DEP_SUBCLASS_IMPL(EVENTTRIGGER, EventTrigger)
AS_DEP_SUBCLASS_IMPL(KEYFRAME, KeyFrame)
AS_DEP_SUBCLASS_IMPL(DOUBLEKEYFRAME, DoubleKeyFrame)
AS_DEP_SUBCLASS_IMPL(POINTKEYFRAME, PointKeyFrame)
AS_DEP_SUBCLASS_IMPL(DISCRETEDOUBLEKEYFRAME, DiscreteDoubleKeyFrame)
AS_DEP_SUBCLASS_IMPL(DISCRETEPOINTKEYFRAME, DiscretePointKeyFrame)
AS_DEP_SUBCLASS_IMPL(LINEARDOUBLEKEYFRAME, LinearDoubleKeyFrame)
AS_DEP_SUBCLASS_IMPL(LINEARPOINTKEYFRAME, LinearPointKeyFrame)
AS_DEP_SUBCLASS_IMPL(POINTANIMATIONUSINGKEYFRAMES, PointAnimationUsingKeyFrames)
AS_DEP_SUBCLASS_IMPL(DOUBLEANIMATIONUSINGKEYFRAMES, DoubleAnimationUsingKeyFrames)

AS_DEP_SUBCLASS_IMPL(COLLECTION, Collection)
AS_DEP_SUBCLASS_IMPL(VISUAL_COLLECTION, VisualCollection)
AS_DEP_SUBCLASS_IMPL(TRIGGER_COLLECTION, TriggerCollection)
AS_DEP_SUBCLASS_IMPL(TRIGGERACTION_COLLECTION, TriggerActionCollection)
AS_DEP_SUBCLASS_IMPL(TRANSFORM_COLLECTION, TransformCollection)
AS_DEP_SUBCLASS_IMPL(GEOMETRY_COLLECTION, GeometryCollection)
AS_DEP_SUBCLASS_IMPL(PATHFIGURE_COLLECTION, PathFigureCollection)
AS_DEP_SUBCLASS_IMPL(PATHSEGMENT_COLLECTION, PathSegmentCollection)



/**
 * item_getbounds:
 * @item: the item to update the bounds of
 *
 * Does this by requesting bounds update to all of its parents. 
 */
void
item_update_bounds (UIElement *item)
{
	double cx1 = item->x1;
	double cy1 = item->y1;
	double cx2 = item->x2;
	double cy2 = item->y2;
	
	item->getbounds ();

	//
	// If we changed, notify the parent to recompute its bounds
	//
	if (item->x1 != cx1 || item->y1 != cy1 || item->y2 != cy2 || item->x2 != cx2){
		if (item->parent != NULL)
			item_update_bounds (item->parent);
	}
}

UIElement::~UIElement ()
{
	printf ("FIXME: We should go through all of the attached properties and unref them\n");
}

void
UIElement::get_xform_for (UIElement *item, cairo_matrix_t *result)
{
	printf ("get_xform_for called on a non-container, you must implement this in your container\n");
	exit (1);
}

void 
item_invalidate (UIElement *item)
{
	double res [6];
	Surface *s = item_get_surface (item);

	if (s == NULL)
		return;

//#define DEBUG_INVALIDATE
#ifdef DEBUG_INVALIDATE
	printf ("Requesting invalidate for %d %d %d %d\n", 
				    (int) item->x1, (int)item->y1, 
				    (int)(item->x2-item->x1+1), (int)(item->y2-item->y1+1));
#endif
	// 
	// Note: this is buggy: why do we need to queue the redraw on the toplevel
	// widget (s->data) and does not work with the drawing area?
	//
	gtk_widget_queue_draw_area ((GtkWidget *)s->drawing_area, 
				    (int) item->x1, (int)item->y1, 
				    (int)(item->x2-item->x1+2), (int)(item->y2-item->y1+2));
}

void 
item_set_transform_origin (UIElement *item, Point p)
{
	item->SetValue (UIElement::RenderTransformOriginProperty, p);
}

void
item_get_render_affine (UIElement *item, cairo_matrix_t *result)
{
	Value* v = item->GetValue (UIElement::RenderTransformProperty);
	if (v == NULL)
		cairo_matrix_init_identity (result);
	else {
		Transform *t = v->AsTransform();
		t->GetTransform (result);
	}
}

void
UIElement::update_xform ()
{
	cairo_matrix_t user_transform;

	//
	// What is more important, the space used by 6 doubles,
	// or the time spent calling the parent (that will call
	// DependencyObject->GetProperty to get the positions?
	//
	// Currently we go for thiner, but if we decide to go
	// for reduced computation, we should introduce the 
	// base transform in UIElement that will be updated by the
	// container on demand
	//
	if (parent != NULL)
		parent->get_xform_for (this, &absolute_xform);
	else
		cairo_matrix_init_identity (&absolute_xform);

	item_get_render_affine (this, &user_transform);

	Point p = getxformorigin ();
	cairo_matrix_translate (&absolute_xform, p.x, p.y);
	cairo_matrix_multiply (&absolute_xform, &user_transform, &absolute_xform);
	cairo_matrix_translate (&absolute_xform, -p.x, -p.y);
}

void
UIElement::OnSubPropertyChanged (DependencyProperty *prop, DependencyProperty *subprop)
{
	if (prop == UIElement::RenderTransformProperty ||
	    prop == UIElement::RenderTransformOriginProperty)
		FullInvalidate (TRUE);
	else if (prop == UIElement::OpacityProperty ||
		 prop == UIElement::ClipProperty ||
		 prop == UIElement::OpacityMaskProperty ||
		 prop == UIElement::VisibilityProperty){
		FullInvalidate (FALSE);
	}
}

//
// Queues the invalidate for the current region, performs any 
// updates to the RenderTransform (optional) and queues a 
// new redraw with the new bounding box
//
void
UIElement::FullInvalidate (bool rendertransform)
{
	item_invalidate (this);
	if (rendertransform)
		update_xform ();
	item_update_bounds (this);
	item_invalidate (this);
}

void
item_set_render_transform (UIElement *item, Transform *transform)
{
	item->SetValue (UIElement::RenderTransformProperty, Value(transform));
}

double
uielement_get_opacity (UIElement *item)
{
	return item->GetValue (UIElement::OpacityProperty)->AsDouble();
}

void
uielement_set_opacity (UIElement *item, double opacity)
{
	item->SetValue (UIElement::OpacityProperty, Value (opacity));
}


FrameworkElement::FrameworkElement ()
{
	triggers = new TriggerCollection ();
}

void 
framework_element_trigger_add (FrameworkElement *element, EventTrigger *trigger)
{
	Value v (trigger);
	element->triggers->Add (&v);
}

double
framework_element_get_height (FrameworkElement *framework_element)
{
	return framework_element->GetValue (FrameworkElement::HeightProperty)->AsDouble();
}

void
framework_element_set_height (FrameworkElement *framework_element, double height)
{
	framework_element->SetValue (FrameworkElement::HeightProperty, Value (height));
}

double
framework_element_get_width (FrameworkElement *framework_element)
{
	return framework_element->GetValue (FrameworkElement::WidthProperty)->AsDouble();
}

void
framework_element_set_width (FrameworkElement *framework_element, double width)
{
	framework_element->SetValue (FrameworkElement::WidthProperty, Value (width));
}

Surface *
item_get_surface (UIElement *item)
{
	if (item->flags & UIElement::IS_CANVAS){
		Canvas *canvas = (Canvas *) item;
		if (canvas->surface)
			return canvas->surface;
	}

	if (item->parent != NULL)
		return item_get_surface (item->parent);

	return NULL;
}

void
VisualCollection::Add (void *data)
{
	Panel *panel = (Panel *) closure;
	
	Value *v = (Value *) data;
	UIElement *item = v->AsUIElement();

	base_ref (item);

	// Add the UIElement, not the Value
	Collection::Add (item);

	item->parent = panel;
	item->update_xform ();
	item_update_bounds (panel);
	item_invalidate (panel);
}

void
VisualCollection::Remove (void *data)
{
	Panel *panel = (Panel *) closure;
	Value *v = (Value *) data;
	UIElement *item = v->AsUIElement();
	
	Collection::Remove (item);

	item_invalidate (item);
	base_unref (item);

	item_update_bounds (panel);
}

void 
panel_child_add (Panel *panel, UIElement *child)
{
	Value v(child);

	collection_add (panel->children, &v);
}

Panel::Panel ()
{
	children = NULL;
	VisualCollection *c = new VisualCollection ();

	this->SetValue (Panel::ChildrenProperty, Value (c));

	// Ensure that the callback OnPropertyChanged was called.
	g_assert (c == children);
}

//
// Intercept any changes to the children property and mirror that into our
// own variable
//
void
Panel::OnPropertyChanged (DependencyProperty *prop)
{
	FrameworkElement::OnPropertyChanged (prop);

	if (prop == ChildrenProperty){
		// The new value has already been set, so unref the old collection

		VisualCollection *newcol = GetValue (prop)->AsVisualCollection();

		if (newcol != children){
			if (children){
				for (GSList *l = children->list; l != NULL; l = l->next){
					DependencyObject *dob = (DependencyObject *) l->data;
					
					base_unref (dob);
				}
				base_unref (children);
				g_slist_free (children->list);
			}

			children = newcol;
			if (children->closure)
				printf ("Warning we attached a property that was already attached\n");
			children->closure = this;
			
			base_ref (children);
		}
	}
}

Canvas::Canvas () : surface (NULL)
{
	flags |= IS_CANVAS;
}

void
Canvas::get_xform_for (UIElement *item, cairo_matrix_t *result)
{
	*result = absolute_xform;

	// Compute left/top if its attached to the item
	Value *val_top = item->GetValue (Canvas::TopProperty);
	double top = val_top == NULL ? 0.0 : val_top->AsDouble();

	Value *val_left = item->GetValue (Canvas::LeftProperty);
	double left = val_left == NULL ? 0.0 : val_left->AsDouble();
		
	cairo_matrix_translate (result, left, top);

	// The RenderTransform and RenderTransformOrigin properties also applies to item drawn on the canvas
	cairo_matrix_t item_transform;
	item_get_render_affine (this, &item_transform);
	cairo_matrix_multiply (result, result, &item_transform);
}

void
Canvas::update_xform ()
{
	UIElement::update_xform ();
	GSList *il;

	for (il = children->list; il != NULL; il = il->next){
		UIElement *item = (UIElement *) il->data;

		item->update_xform ();
	}
}

void
Canvas::getbounds ()
{
	bool first = TRUE;
	GSList *il;

	for (il = children->list; il != NULL; il = il->next){
		UIElement *item = (UIElement *) il->data;

		item->getbounds ();
		if (first){
			x1 = item->x1;
			x2 = item->x2;
			y1 = item->y1;
			y2 = item->y2;
			first = FALSE;
			continue;
		} 

		if (item->x1 < x1)
			x1 = item->x1;
		if (item->x2 > x2)
			x2 = item->x2;
		if (item->y1 < y1)
			y1 = item->y1;
		if (item->y2 > y2)
			y2 = item->y2;
	}

	// If we found nothing.
	if (first){
		x1 = y1 = x2 = y2 = 0;
	}
}

void 
Canvas::OnSubPropertyChanged (DependencyProperty *prop, DependencyProperty *subprop)
{
	printf ("Prop %s changed in %s\n", prop->name, subprop->name);
}

void 
surface_clear (Surface *s, int x, int y, int width, int height)
{
	static unsigned char n;
	cairo_matrix_t identity;

	cairo_matrix_init_identity (&identity);

	cairo_set_matrix (s->cairo, &identity);

	cairo_set_source_rgba (s->cairo, 0.7, 0.7, 0.7, 1.0);
	cairo_rectangle (s->cairo, x, y, width, height);
	cairo_fill (s->cairo);
}
	
void
surface_clear_all (Surface *s)
{
	memset (s->buffer, 0, s->width * s->height * 4);
}

static void
surface_realloc (Surface *s)
{
	if (s->buffer)
		free (s->buffer);

	int size = s->width * s->height * 4;
	s->buffer = (unsigned char *) malloc (size);
	surface_clear_all (s);
       
	s->cairo_buffer_surface = cairo_image_surface_create_for_data (
		s->buffer, CAIRO_FORMAT_ARGB32, s->width, s->height, s->width * 4);

	s->cairo_buffer = cairo_create (s->cairo_buffer_surface);
	s->cairo = s->cairo_buffer;
}

void 
surface_destroy (Surface *s)
{
	if (s->toplevel){
		base_unref (s->toplevel);
		s->toplevel = NULL;
	}

	cairo_destroy (s->cairo_buffer);
	if (s->cairo_xlib)
		cairo_destroy (s->cairo_xlib);
	s->cairo_xlib = NULL;
	s->cairo_buffer = NULL;

	if (s->pixmap != NULL)
		gdk_pixmap_unref (s->pixmap);
	s->pixmap = NULL;
	cairo_surface_destroy (s->cairo_buffer_surface);
	if (s->xlib_surface)
		cairo_surface_destroy (s->xlib_surface);
	s->cairo_buffer_surface = NULL;

	if (s->drawing_area != NULL){
		gtk_widget_destroy (s->drawing_area);
		s->drawing_area = NULL;
	}
	// TODO: add everything
	delete s;
}

void
create_xlib (Surface *s, GtkWidget *widget)
{
	s->pixmap = gdk_pixmap_new (GDK_DRAWABLE (widget->window), s->width, s->height, -1);

	s->xlib_surface = cairo_xlib_surface_create (
		GDK_WINDOW_XDISPLAY(widget->window),
		GDK_WINDOW_XWINDOW(GDK_DRAWABLE (s->pixmap)),
		GDK_VISUAL_XVISUAL (gdk_window_get_visual(widget->window)),
		s->width, s->height);

	s->cairo_xlib = cairo_create (s->xlib_surface);
}

gboolean
realized_callback (GtkWidget *widget, gpointer data)
{
	Surface *s = (Surface *) data;
	cairo_surface_t *xlib;

	create_xlib (s, widget);

	s->cairo = s->cairo_xlib;
}

gboolean
unrealized_callback (GtkWidget *widget, gpointer data)
{
	Surface *s = (Surface *) data;

	cairo_surface_destroy(s->xlib_surface);
	s->xlib_surface = NULL;

	s->cairo = s->cairo_buffer;
}

gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
	Surface *s = (Surface *) data;

	s->frames++;

	if (event->area.x > s->width || event->area.y > s->height)
		return TRUE;

	//
	// BIG DEBUG BLOB
	// 
	if (cairo_status (s->cairo) != CAIRO_STATUS_SUCCESS){
		printf ("expose event: the cairo context has an error condition and refuses to paint: %s\n", 
			cairo_status_to_string (cairo_status (s->cairo)));
	}

#ifdef DEBUG_INVALIDATE
	printf ("Got a request to repaint at %d %d %d %d\n", event->area.x, event->area.y, event->area.width, event->area.height);
#endif

	surface_repaint (s, event->area.x, event->area.y, event->area.width, event->area.height);
	gdk_draw_drawable (
		widget->window, gtk_widget_get_style (widget)->white_gc, s->pixmap, 
		event->area.x, event->area.y, // gint src_x, gint src_y,
		event->area.x, event->area.y, // gint dest_x, gint dest_y,
		MIN (event->area.width, s->width),
		MIN (event->area.height, s->height));

	return TRUE;
}

void
Canvas::render (Surface *s, int x, int y, int width, int height)
{
	GSList *il;
	double actual [6];
	
	for (il = children->list; il != NULL; il = il->next){
		UIElement *item = (UIElement *) il->data;

		item->render (s, x, y, width, height);

		if (!(item->flags & UIElement::IS_LOADED)) {
			item->flags |= UIElement::IS_LOADED;
			item->events->Emit ("Loaded");
		}
	}

	if (!(flags & UIElement::IS_LOADED)) {
		flags |= UIElement::IS_LOADED;
		events->Emit ("Loaded");
	}
}

Canvas *
canvas_new ()
{
	return new Canvas ();
}

void 
clear_drawing_area (GtkObject *obj, gpointer data)
{
	Surface *s = (Surface *) data;

	s->drawing_area = NULL;
}

Surface *
surface_new (int width, int height)
{
	Surface *s = new Surface ();

	s->drawing_area = gtk_drawing_area_new ();
	gtk_widget_set_double_buffered (s->drawing_area, FALSE);

	gtk_widget_show (s->drawing_area);

	gtk_widget_set_usize (s->drawing_area, width, height);
	s->buffer = NULL;
	s->width = width;
	s->height = height;
	s->toplevel = NULL;

	surface_realloc (s);

	gtk_signal_connect (GTK_OBJECT (s->drawing_area), "expose_event",
			    G_CALLBACK (expose_event_callback), s);

	gtk_signal_connect (GTK_OBJECT (s->drawing_area), "realize",
			    G_CALLBACK (realized_callback), s);

	gtk_signal_connect (GTK_OBJECT (s->drawing_area), "unrealize",
			    G_CALLBACK (unrealized_callback), s);

	gtk_signal_connect (GTK_OBJECT (s->drawing_area), "destroy",
			    G_CALLBACK (clear_drawing_area), s);

	return s;
}

void
surface_attach (Surface *surface, UIElement *toplevel)
{
	if (!(toplevel->flags & UIElement::IS_CANVAS)){
		printf ("Unsupported toplevel\n");
		return;
	}
	if (surface->toplevel){
		item_invalidate (surface->toplevel);
		base_unref (surface->toplevel);
	}

	Canvas *canvas = (Canvas *) toplevel;
	base_ref (canvas);

	canvas->surface = surface;
	surface->toplevel = canvas;

	item_update_bounds (canvas);
	item_invalidate (canvas);
}

void
surface_repaint (Surface *s, int x, int y, int width, int height)
{
	surface_clear (s, x, y, width, height);
	s->toplevel->render (s, x, y, width, height);
}

void *
surface_get_drawing_area (Surface *s)
{
	return s->drawing_area;
}

/*
	DependencyObject
*/

GHashTable *DependencyObject::properties = NULL;

typedef struct {
	DependencyObject *dob;
	DependencyProperty *prop;
} Attachee;

void
DependencyObject::NotifyAttacheesOfPropertyChange (DependencyProperty *subproperty)
{
	for (GSList *l = attached_list; l != NULL; l = l->next){
		Attachee *att = (Attachee*)l->data;

		att->dob->OnSubPropertyChanged (att->prop, subproperty);
	}
}

void
DependencyObject::SetValue (DependencyProperty *property, Value *value)
{
	g_return_if_fail (property != NULL);

	if (property->value_type < Value::DEPENDENCY_OBJECT)
		g_return_if_fail (property->value_type == value->k);

	Value *current_value = (Value*)g_hash_table_lookup (current_values, property->name);

	if ((current_value == NULL && value != NULL) ||
	    (current_value != NULL && value == NULL) ||
	    (current_value != NULL && value != NULL && *current_value != *value)) {

		if (current_value != NULL && current_value->k >= Value::DEPENDENCY_OBJECT){
			DependencyObject *dob = current_value->AsDependencyObject();

			for (GSList *l = dob->attached_list; l; l = l->next) {
				Attachee *att = (Attachee*)l->data;
				if (att->dob == this && att->prop == property) {
					dob->attached_list = g_slist_remove_link (dob->attached_list, l);
					delete att;
					break;
				}
			}
		}

		Value *store = value ? new Value (*value) : NULL;

		g_hash_table_insert (current_values, property->name, store);

		if (value) {
			if (value->k >= Value::DEPENDENCY_OBJECT){
				DependencyObject *dob = value->AsDependencyObject();
				Attachee *att = new Attachee ();
				att->dob = this;
				att->prop = property;
				dob->attached_list = g_slist_append (dob->attached_list, att);
			}

			// 
			//NotifyAttacheesOfPropertyChange (property);
		}

		OnPropertyChanged (property);
	}
}

void
DependencyObject::SetValue (DependencyProperty *property, Value value)
{
	SetValue (property, &value);
}

Value *
DependencyObject::GetValue (DependencyProperty *property)
{
	Value *value = NULL;

	value = (Value *) g_hash_table_lookup (current_values, property->name);

	if (value != NULL)
		return value;

	return property->default_value;
}

static void
free_value (void *v)
{
	Value *val = (Value*)v;
	delete val;
}

DependencyObject::DependencyObject ()
{
	current_values = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, free_value);
	events = new EventObject ();
	this->attached_list = NULL;
}

DependencyObject::~DependencyObject ()
{
	g_hash_table_destroy (current_values);
	delete events;
}

DependencyProperty *
DependencyObject::GetDependencyProperty (char *name)
{
	return DependencyObject::GetDependencyProperty (GetObjectType (), name);
}

DependencyProperty *
DependencyObject::GetDependencyProperty (Value::Kind type, char *name)
{
	GHashTable *table;
	DependencyProperty *property;

	if (properties == NULL)
		return NULL;

	table = (GHashTable*) g_hash_table_lookup (properties, &type);

	if (table == NULL)
		return NULL;

	property = (DependencyProperty*) g_hash_table_lookup (table, name);

	if (property != NULL)
		return property;

	// Look in the parent type
	Type *current_type;
	current_type = Type::Find (type);
	
	if (current_type == NULL)
		return NULL;
	
	if (current_type->parent == Value::INVALID)
		return NULL;

	return GetDependencyProperty (current_type->parent, name);
}

DependencyObject*
DependencyObject::FindName (char *name)
{
	NameScope *scope = NameScope::GetNameScope (this);
	if (!scope)
		scope = global_NameScope;

	return scope->FindName (name);
}

//
// Use this for values that can be null
//
DependencyProperty *
DependencyObject::Register (Value::Kind type, char *name, Value::Kind vtype)
{
	g_return_val_if_fail (name != NULL, NULL);

	return RegisterFull (type, name, NULL, vtype);
}

//
// DependencyObject takes ownership of the Value * for default_value
//
DependencyProperty *
DependencyObject::Register (Value::Kind type, char *name, Value *default_value)
{
	g_return_val_if_fail (default_value != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	return RegisterFull (type, name, default_value, default_value->k);
}

//
// Register the dependency property that belongs to @type with the name @name
// The default value is @default_value (if provided) and the type that can be
// stored in the dependency property is of type @vtype
//
DependencyProperty *
DependencyObject::RegisterFull (Value::Kind type, char *name, Value *default_value, Value::Kind vtype)
{
	GHashTable *table;

	DependencyProperty *property = new DependencyProperty (type, name, default_value, vtype);
	
	/* first add the property to the global 2 level property hash */
	if (NULL == properties)
		properties = g_hash_table_new (g_int_hash, g_int_equal);

	table = (GHashTable*) g_hash_table_lookup (properties, &property->type);

	if (table == NULL) {
		table = g_hash_table_new (g_str_hash, g_str_equal);
		g_hash_table_insert (properties, &property->type, table);
	}

	g_hash_table_insert (table, property->name, property);

	return property;
}

Value *
dependency_object_get_value (DependencyObject *object, DependencyProperty *prop)
{
	return object->GetValue (prop);
}

void
dependency_object_set_value (DependencyObject *object, DependencyProperty *prop, Value *val)
{
	object->SetValue (prop, val);
}

/*
 *	DependencyProperty
 */
DependencyProperty::DependencyProperty (Value::Kind type, char *name, Value *default_value, Value::Kind value_type)
{
	this->type = type;
	this->name = g_strdup (name);
	this->default_value = default_value;
	this->value_type = value_type;
}

DependencyProperty::~DependencyProperty ()
{
	g_free (name);
	if (default_value != NULL)
		g_free (default_value);
}

DependencyProperty *dependency_property_lookup (Value::Kind type, char *name)
{
	return DependencyObject::GetDependencyProperty (type, name);
}

// event handlers for c++
typedef struct {
	EventHandler func;
	gpointer data;
} EventClosure;

EventObject::EventObject ()
{
	event_hash = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
free_closure_list (gpointer key, gpointer data, gpointer userdata)
{
	g_free (key);
	g_list_foreach ((GList*)data, (GFunc)g_free, NULL);
}

EventObject::~EventObject ()
{
	g_hash_table_foreach (event_hash, free_closure_list, NULL);
}

void
EventObject::AddHandler (char *event_name, EventHandler handler, gpointer data)
{
	GList *events = (GList*)g_hash_table_lookup (event_hash, event_name);

	EventClosure *closure = new EventClosure ();
	closure->func = handler;
	closure->data = data;

	if (events == NULL) {
		g_hash_table_insert (event_hash, g_strdup (event_name), g_list_prepend (NULL, closure));
	}
	else {
		events = g_list_append (events, closure); // not prepending means we don't need to g_hash_table_replace
	}
}

void
EventObject::RemoveHandler (char *event_name, EventHandler handler, gpointer data)
{
	GList *events = (GList*)g_hash_table_lookup (event_hash, event_name);

	if (events == NULL)
		return;

	GList *l;
	for (l = events; l; l = l->next) {
		EventClosure *closure = (EventClosure*)l->data;
		if (closure->func == handler && closure->data == data)
			break;
	}

	if (l == NULL) /* we didn't find it */
		return;

	g_free (l->data);
	events = g_list_delete_link (events, l);

	if (events == NULL) {
		/* delete the event */
		gpointer key, value;
		g_hash_table_lookup_extended (event_hash, event_name, &key, &value);
		g_free (key);
	}
	else {
		g_hash_table_replace (event_hash, event_name, events);
	}
}

void
EventObject::Emit (char *event_name)
{
	GList *events = (GList*)g_hash_table_lookup (event_hash, event_name);

	if (events == NULL)
		return;
	
	for (GList *l = events; l; l = l->next) {
		EventClosure *closure = (EventClosure*)l->data;
		closure->func (closure->data);
	}
}

NameScope::NameScope ()
{
	names = g_hash_table_new (g_str_hash, g_str_equal);
}

NameScope::~NameScope ()
{
  //	g_hash_table_foreach (/* XXX */);
}

void
NameScope::RegisterName (const char *name, DependencyObject *object)
{
	g_hash_table_insert (names, g_strdup (name) ,object);
}

void
NameScope::UnregisterName (const char *name)
{
}

DependencyObject*
NameScope::FindName (const char *name)
{
	return (DependencyObject*)g_hash_table_lookup (names, name);
}

NameScope*
NameScope::GetNameScope (DependencyObject *obj)
{
	Value *v = obj->GetValue (NameScope::NameScopeProperty);
	return v == NULL ? NULL : v->AsNameScope();
}

void
SetNameScope (DependencyObject *obj, NameScope *scope)
{
	obj->SetValue (NameScope::NameScopeProperty, scope);
}

void
TriggerCollection::Add (void *data)
{
	FrameworkElement *fwe = (FrameworkElement *) closure;
	
	Value *v = (Value *) data;
	EventTrigger *trigger = v->AsEventTrigger ();

	Collection::Add (trigger);

	trigger->SetTarget (fwe);
}

void
TriggerCollection::Remove (void *data)
{
	FrameworkElement *fwe = (FrameworkElement *) closure;
	
	Value *v = (Value *) data;
	EventTrigger *trigger = v->AsEventTrigger ();

	Collection::Remove (trigger);
}

void
TriggerActionCollection::Add (void *data)
{
	EventTrigger *trigger = (EventTrigger *) closure;
	
	Value *v = (Value *) data;
	TriggerAction *action = v->AsTriggerAction ();

	Collection::Add (action);
}

void
TriggerActionCollection::Remove (void *data)
{
	EventTrigger *trigger = (EventTrigger *) closure;
	
	Value *v = (Value *) data;
	TriggerAction *action = v->AsTriggerAction ();

	Collection::Remove (action);
}


EventTrigger::EventTrigger () : routed_event (NULL)
{
	actions = new TriggerActionCollection ();
}

void
EventTrigger::SetTarget (DependencyObject *target)
{
	g_assert (target);

	// Despite the name, it can only be loaded (according to the docs)
	target->events->AddHandler ("Loaded", (EventHandler) event_trigger_fire_actions, this);
}

void
EventTrigger::RemoveTarget (DependencyObject *target)
{
	g_assert (target);

	target->events->RemoveHandler ("Loaded", (EventHandler) event_trigger_fire_actions, this);
}

EventTrigger *
event_trigger_new ()
{
	return new EventTrigger ();
}

void
event_trigger_action_add (EventTrigger *trigger, TriggerAction *action)
{
	Value v (action);
	trigger->actions->Add (&v);
}

void
event_trigger_fire_actions (EventTrigger *trigger)
{
	g_assert (trigger);

	for (GSList *walk = trigger->actions->list; walk != NULL; walk = walk->next) {
		TriggerAction *action = (TriggerAction *) walk->data;
		action->Fire ();
	}
}

//
// UIElement
//
DependencyProperty* UIElement::RenderTransformProperty;
DependencyProperty* UIElement::OpacityProperty;
DependencyProperty* UIElement::ClipProperty;
DependencyProperty* UIElement::TriggersProperty;
DependencyProperty* UIElement::OpacityMaskProperty;
DependencyProperty* UIElement::RenderTransformOriginProperty;
DependencyProperty* UIElement::CursorProperty;
DependencyProperty* UIElement::IsHitTestVisibleProperty;
DependencyProperty* UIElement::VisibilityProperty;
DependencyProperty* UIElement::ResourcesProperty;

void
item_init ()
{
	UIElement::RenderTransformProperty = DependencyObject::Register (Value::UIELEMENT, "RenderTransform", Value::TRANSFORM);
	UIElement::OpacityProperty = DependencyObject::Register (Value::UIELEMENT, "Opacity", new Value(1.0));
	UIElement::ClipProperty = DependencyObject::Register (Value::UIELEMENT, "Clip", Value::GEOMETRY);
	UIElement::TriggersProperty = DependencyObject::Register (Value::UIELEMENT, "Triggers", Value::TRIGGER_COLLECTION);
	UIElement::OpacityMaskProperty = DependencyObject::Register (Value::UIELEMENT, "OpacityMask", Value::BRUSH);
	UIElement::RenderTransformOriginProperty = DependencyObject::Register (Value::UIELEMENT, "RenderTransformOrigin", Value::POINT);
	UIElement::CursorProperty = DependencyObject::Register (Value::UIELEMENT, "Cursor", Value::INT32);
	UIElement::IsHitTestVisibleProperty = DependencyObject::Register (Value::UIELEMENT, "IsHitTestVisible", Value::BOOL);
	UIElement::VisibilityProperty = DependencyObject::Register (Value::UIELEMENT, "Visibility", Value::INT32);
	UIElement::ResourcesProperty = DependencyObject::Register (Value::UIELEMENT, "Resources", Value::RESOURCE_COLLECTION);
}

//
// Namescope
//
DependencyProperty *NameScope::NameScopeProperty;

void
namescope_init ()
{
	global_NameScope = new NameScope ();
	NameScope::NameScopeProperty = DependencyObject::Register (Value::NAMESCOPE, "NameScope", Value::NAMESCOPE);
}

DependencyProperty* FrameworkElement::HeightProperty;
DependencyProperty* FrameworkElement::WidthProperty;

void
framework_element_init ()
{
	FrameworkElement::HeightProperty = DependencyObject::Register (Value::FRAMEWORKELEMENT, "Height", new Value (0.0));
	FrameworkElement::WidthProperty = DependencyObject::Register (Value::FRAMEWORKELEMENT, "Width", new Value (0.0));
}

DependencyProperty* Panel::ChildrenProperty;
DependencyProperty* Panel::BackgroundProperty;

void 
panel_init ()
{
	Panel::ChildrenProperty = DependencyObject::Register (Value::PANEL, "Children", Value::VISUAL_COLLECTION);
	Panel::BackgroundProperty = DependencyObject::Register (Value::PANEL, "Background", Value::BRUSH);
}

DependencyProperty* Canvas::TopProperty;
DependencyProperty* Canvas::LeftProperty;

void 
canvas_init ()
{
	Canvas::TopProperty = DependencyObject::Register (Value::CANVAS, "Top", new Value (0.0));
	Canvas::LeftProperty = DependencyObject::Register (Value::CANVAS, "Left", new Value (0.0));
}

Type* Type::types [];
GHashTable* Type::types_by_name = NULL;

Type *
Type::RegisterType (char *name, Value::Kind type)
{
	return RegisterType (name, type, Value::INVALID);
}

Type *
Type::RegisterType (char *name, Value::Kind type, Value::Kind parent)
{
	if (types == NULL) {
		memset (&types, 0, Value::LASTTYPE * sizeof (Type*));
	}
	if (types_by_name == NULL) {
		types_by_name = g_hash_table_new (g_str_hash, g_str_equal);
	}

	Type *result = new Type ();
	result->name = g_strdup (name);
	result->type = type;
	result->parent = parent;

	g_assert (types [type] == NULL);

	types [type] = result;
	g_hash_table_insert (types_by_name, result, result->name);

	return result;
}

bool 
Type::IsSubclassOf (Value::Kind super)
{
	if (type == super)
		return true;

	if (parent == super)
		return true;

	if (parent == Value::INVALID)
		return false;

	Type *parent_type = Find (parent);
	
	if (parent_type == NULL)
		return false;
	
	return parent_type->IsSubclassOf (super);
}

Type *
Type::Find (char *name)
{
	Type *result;

	if (types == NULL)
		return NULL;

	result = (Type*) g_hash_table_lookup (types_by_name, &name);

	return result;
}

Type *
Type::Find (Value::Kind type)
{
	return types [type];
}

void 
types_init ()
{
	//Type::RegisterType ("Invalid", Value::INVALID, Value::INVALID);
	Type::RegisterType ("bool", Value::BOOL);
	Type::RegisterType ("double", Value::DOUBLE);
	Type::RegisterType ("uint64", Value::UINT64);
	Type::RegisterType ("int", Value::INT32);
	Type::RegisterType ("string", Value::STRING);
	Type::RegisterType ("Color", Value::COLOR);
	Type::RegisterType ("Point", Value::POINT);
	Type::RegisterType ("Rect", Value::RECT);
	Type::RegisterType ("RepeatBehaviour", Value::REPEATBEHAVIOR);
	Type::RegisterType ("Duration", Value::DURATION);
	Type::RegisterType ("int64", Value::INT64);
	Type::RegisterType ("KeyTime", Value::KEYTIME);
	Type::RegisterType ("double*", Value::DOUBLE_ARRAY);
	Type::RegisterType ("Point*", Value::POINT_ARRAY);

	Type::RegisterType ("DependencyObject", Value::DEPENDENCY_OBJECT);

	// These are dependency objects
	Type::RegisterType ("UIElement", Value::UIELEMENT, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Panel", Value::PANEL, Value::FRAMEWORKELEMENT);
	Type::RegisterType ("Canvas", Value::CANVAS, Value::PANEL);
	Type::RegisterType ("Timeline", Value::TIMELINE, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("TimelineGroup", Value::TIMELINEGROUP, Value::TIMELINE);
	Type::RegisterType ("ParallelTimeline", Value::PARALLELTIMELINE, Value::TIMELINEGROUP);
	Type::RegisterType ("Transform", Value::TRANSFORM, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("TransformGroup", Value::TRANSFORMGROUP, Value::TRANSFORM);
	Type::RegisterType ("RotateTransform", Value::ROTATETRANSFORM, Value::TRANSFORM);
	Type::RegisterType ("ScaleTransform", Value::SCALETRANSFORM, Value::TRANSFORM);
	Type::RegisterType ("TranslateTransform", Value::TRANSLATETRANSFORM, Value::TRANSFORM);
	Type::RegisterType ("MatrixTransform", Value::MATRIXTRANSFORM, Value::TRANSFORM);
	Type::RegisterType ("Storyboard", Value::STORYBOARD, Value::PARALLELTIMELINE);
	Type::RegisterType ("Animation", Value::ANIMATION, Value::TIMELINE);
	Type::RegisterType ("DoubleAnimation", Value::DOUBLEANIMATION, Value::ANIMATION);
	Type::RegisterType ("ColorAnimation", Value::COLORANIMATION, Value::ANIMATION);
	Type::RegisterType ("PointAnimation", Value::POINTANIMATION, Value::ANIMATION);
	Type::RegisterType ("Shape", Value::SHAPE, Value::FRAMEWORKELEMENT);
	Type::RegisterType ("Ellipse", Value::ELLIPSE, Value::SHAPE);
	Type::RegisterType ("Line", Value::LINE, Value::SHAPE);
	Type::RegisterType ("Path", Value::PATH, Value::SHAPE);
	Type::RegisterType ("Polygon", Value::POLYGON, Value::SHAPE);
	Type::RegisterType ("Polyline", Value::POLYLINE, Value::SHAPE);
	Type::RegisterType ("Rectangle", Value::RECTANGLE, Value::SHAPE);
	Type::RegisterType ("Geometry", Value::GEOMETRY, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("GeometryGroup", Value::GEOMETRYGROUP, Value::GEOMETRY);
	Type::RegisterType ("EllipseGeometry", Value::ELLIPSEGEOMETRY, Value::GEOMETRY);
	Type::RegisterType ("LineGeometry", Value::LINEGEOMETRY, Value::GEOMETRY);
	Type::RegisterType ("PathGeometry", Value::PATHGEOMETRY, Value::GEOMETRY);
	Type::RegisterType ("RectangleGeometry", Value::RECTANGLEGEOMETRY, Value::GEOMETRY);
	Type::RegisterType ("FrameworkElement", Value::FRAMEWORKELEMENT, Value::UIELEMENT);
	Type::RegisterType ("Namescope", Value::NAMESCOPE, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Clock", Value::CLOCK, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("AnimationClock", Value::ANIMATIONCLOCK, Value::CLOCK);
	Type::RegisterType ("ClockGroup", Value::CLOCKGROUP, Value::CLOCK);
	Type::RegisterType ("Brush", Value::BRUSH, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("SolidColorBrush", Value::SOLIDCOLORBRUSH, Value::BRUSH);
	Type::RegisterType ("TileBrush", Value::TILEBRUSH, Value::BRUSH);
	Type::RegisterType ("ImageBrush", Value::IMAGEBRUSH, Value::TILEBRUSH);
	Type::RegisterType ("VideoBrush", Value::VIDEOBRUSH, Value::TILEBRUSH);
	Type::RegisterType ("GradientBrush", Value::GRADIENTBRUSH, Value::BRUSH);
	Type::RegisterType ("LinearGradientBrush", Value::LINEARGRADIENTBRUSH, Value::GRADIENTBRUSH);
	Type::RegisterType ("GradientStop", Value::GRADIENTSTOP, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("PathFigure", Value::PATHFIGURE, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("PathSegment", Value::PATHSEGMENT, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("ArcSegment", Value::ARCSEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("BezierSegment", Value::BEZIERSEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("LineSegment", Value::LINESEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("PolyBezierSegment", Value::POLYBEZIERSEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("PolylineSegment", Value::POLYLINESEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("PolyquadraticBezierSegment", Value::POLYQUADRATICBEZIERSEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("QuadraticBezierSegment", Value::QUADRATICBEZIERSEGMENT, Value::PATHSEGMENT);
	Type::RegisterType ("TriggerAction", Value::TRIGGERACTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("BeginStoryboard", Value::BEGINSTORYBOARD, Value::TRIGGERACTION);
	Type::RegisterType ("EventTrigger", Value::EVENTTRIGGER, Value::DEPENDENCY_OBJECT);

	Type::RegisterType ("KeyFrame", Value::KEYFRAME, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("ColorKeyFrame", Value::COLORKEYFRAME, Value::KEYFRAME);
	Type::RegisterType ("DoubleKeyFrame", Value::DOUBLEKEYFRAME, Value::KEYFRAME);
	Type::RegisterType ("PointKeyFrame", Value::POINTKEYFRAME, Value::KEYFRAME);
	Type::RegisterType ("DiscreteColorKeyFrame", Value::DISCRETECOLORKEYFRAME, Value::DOUBLEKEYFRAME);
	Type::RegisterType ("DiscreteDoubleKeyFrame", Value::DISCRETEDOUBLEKEYFRAME, Value::DOUBLEKEYFRAME);
	Type::RegisterType ("DiscretePointKeyFrame", Value::DISCRETEPOINTKEYFRAME, Value::POINTKEYFRAME);
	Type::RegisterType ("LinearColorKeyFrame", Value::LINEARCOLORKEYFRAME, Value::COLORKEYFRAME);
	Type::RegisterType ("LinearDoubleKeyFrame", Value::LINEARDOUBLEKEYFRAME, Value::DOUBLEKEYFRAME);
	Type::RegisterType ("LinearPointKeyFrame", Value::LINEARPOINTKEYFRAME, Value::POINTKEYFRAME);
	Type::RegisterType ("ColorAnimationUsingKeyFrames", Value::COLORANIMATIONUSINGKEYFRAMES, Value::COLORANIMATION);
	Type::RegisterType ("DoubleAnimationUsingKeyFrames", Value::DOUBLEANIMATIONUSINGKEYFRAMES, Value::DOUBLEANIMATION);
	Type::RegisterType ("PointAnimationUsingKeyFrames", Value::POINTANIMATIONUSINGKEYFRAMES, Value::POINTANIMATION);

	// The collections
	Type::RegisterType ("Collection", Value::COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Stroke_Collection", Value::STROKE_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Inlines", Value::INLINES, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Styluspoint_Collection", Value::STYLUSPOINT_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Keyframe_Collection", Value::KEYFRAME_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("TimelineMarker_Collection", Value::TIMELINEMARKER_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Geometry_Collection", Value::GEOMETRY_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("GradientStop_Collection", Value::GRADIENTSTOP_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("MediaAttribute_Collection", Value::MEDIAATTRIBUTE_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("PathFigure_Collection", Value::PATHFIGURE_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("PathSegment_Collection", Value::PATHSEGMENT_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Timeline_Collection", Value::TIMELINE_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Transform_Collection", Value::TRANSFORM_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Visual_Collection", Value::VISUAL_COLLECTION, Value::COLLECTION);
	Type::RegisterType ("Resource_Collection", Value::RESOURCE_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("TriggerAction_Collection", Value::TRIGGERACTION_COLLECTION, Value::DEPENDENCY_OBJECT);
	Type::RegisterType ("Trigger_Collection", Value::TRIGGER_COLLECTION, Value::DEPENDENCY_OBJECT);

//#if DEBUG
	//printf ("Checking types...\n");
	for (int i = 1; i < Value::LASTTYPE; i++) {
		if (Type::types [i] == NULL)
			printf ("Type %i is not initialized\n", i);
	}
//#endif
}

static bool inited = FALSE;

void
runtime_init ()
{
	if (inited)
		return;
	inited = TRUE;

	types_init ();
	namescope_init ();
	item_init ();
	framework_element_init ();
	panel_init ();
	canvas_init ();
	transform_init ();
	animation_init ();
	brush_init ();
	shape_init ();
	geometry_init ();
	xaml_init ();
}
