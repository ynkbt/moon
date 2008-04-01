/*
 * Automatically generated from type.cpp.in, do not edit this file directly
 * To regenerate execute typegen.sh
*/

/*
 * type.cpp: Generated code for the type system.
 *
 * Author:
 *   Rolf Bjarne Kvinge (RKvinge@novell.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#include <cairo.h>
#include <malloc.h>
#include <stdlib.h>
#include "type.h"

#include "runtime.h"
#include "canvas.h"
#include "control.h"
#include "color.h"
#include "shape.h"
#include "transform.h"
#include "animation.h"
#include "downloader.h"
#include "frameworkelement.h"
#include "stylus.h"
#include "rect.h"
#include "text.h"
#include "panel.h"
#include "value.h"
#include "namescope.h"
#include "xaml.h"


bool types_initialized = false;

/*
	Type implementation
*/

Type** Type::types = NULL;
GHashTable* Type::types_by_name = NULL;

Type::Type (const char *name, Type::Kind type, Type::Kind parent)
{
	this->name = strdup (name);
	this->type = type;
	this->parent = parent;

	local_event_count = 0;
	local_event_base = -1;
	type_event_count = -1;
	event_name_hash = NULL;
}

Type::~Type()
{
	if (event_name_hash)
		g_hash_table_destroy (event_name_hash);

	event_name_hash = NULL;

	free (name);
}

void
Type::HideEvent (const char *event_name)
{
	if (event_name_hash)
		g_hash_table_remove (event_name_hash, event_name);
}

void
Type::RegisterEvent (const char *event_name)
{
	if (event_name_hash == NULL)
		event_name_hash = g_hash_table_new_full (strcase_hash, strcase_equal,
							 (GDestroyNotify)g_free,
							 NULL);

	g_hash_table_insert (event_name_hash, g_strdup (event_name), GINT_TO_POINTER (local_event_count++));
}

int
Type::LookupEvent (const char *event_name)
{
	gpointer key, value;
	if (event_name_hash &&
	    g_hash_table_lookup_extended (event_name_hash,
					  event_name,
					  &key,
					  &value)) {

		return GPOINTER_TO_INT (value) + GetEventBase();
	}
	
	if (parent == Type::INVALID) {
		printf ("type lookup of event '%s' failed\n", event_name);
		return -1;
	}
	else {
		return Type::Find (parent)->LookupEvent (event_name);
	}
}

int
Type::GetEventBase ()
{
	if (local_event_base == -1) {
		if (parent == Type::INVALID)
			local_event_base = 0;
		else
			local_event_base = Type::Find(parent)->GetEventCount();
	}

	return local_event_base;
}

int
Type::GetEventCount ()
{
	if (type_event_count == -1)
		type_event_count = GetEventBase() + local_event_count;

	return type_event_count;
}

Type *
Type::RegisterType (const char *name, Type::Kind type, bool value_type)
{
	return RegisterType (name, type, Type::INVALID, NULL, NULL, value_type);
}

Type *
Type::RegisterType (const char *name, Type::Kind type, Type::Kind parent)
{
	return RegisterType (name, type, parent, NULL, NULL, false);
}

void
Type::free_type (gpointer type)
{
	delete (Type*)type;
}

Type *
Type::RegisterType (const char *name, Type::Kind type, Type::Kind parent, create_inst_func *create_inst, const char *content_property)
{
	return RegisterType (name, type, parent, create_inst, content_property, false);
}

Type *
Type::RegisterType (const char *name, Type::Kind type, Type::Kind parent, create_inst_func *create_inst, const char *content_property, bool value_type)
{
	if (types == NULL) {
		types = (Type**)calloc (Type::LASTTYPE, sizeof (Type*));
	}
	if (types_by_name == NULL) {
		types_by_name = g_hash_table_new_full (strcase_hash, strcase_equal,
						       NULL, free_type);
	}

	g_return_val_if_fail (types[type] == NULL, NULL);

	Type *result = new Type (name, type, parent);
	result->value_type = value_type;

	types [type] = result;
	g_hash_table_insert (types_by_name, result->name, result);

	result->create_inst = create_inst;
	result->content_property = content_property;

	return result;
}

bool 
Type::IsSubclassOf (Type::Kind super)
{
	if (type == super)
		return true;

	if (parent == super)
		return true;

	if (parent == Type::INVALID)
		return false;

	Type *parent_type = Find (parent);
	
	if (parent_type == NULL)
		return false;
	
	return parent_type->IsSubclassOf (super);
}

Type *
Type::Find (const char *name)
{
	Type *result;

	if (!types_initialized) {
		fprintf (stderr, "Warning: Moonlight type system is accessed after it has shutdown. It will be reinitialized.\n");
		types_init ();
	}

	if (types_by_name == NULL)
		return NULL;

	result = (Type*) g_hash_table_lookup (types_by_name, name);

	return result;
}

Type *
Type::Find (Type::Kind type)
{
	if (!types_initialized) {
		fprintf (stderr, "Warning: Moonlight type system is accessed after it has shutdown. It will be reinitialized.\n");
		types_init ();
	}

	return types [type];
}

DependencyObject *
Type::CreateInstance ()
{
        if (!create_inst) {
                g_warning ("Unable to create an instance of type: %s\n", name);
                return NULL;
        }

        return create_inst ();
}

const char *
Type::GetContentPropertyName ()
{
	if (content_property)
		return content_property;

	if (parent == Type::INVALID)
		return NULL;

	return Find (parent)->GetContentPropertyName ();
}

void
Type::Shutdown ()
{
	if (types) {
		g_free (types);
		types = NULL;
	}
	if (types_by_name) {
		g_hash_table_destroy (types_by_name);
		types_by_name = NULL;
	}

	types_initialized = false;
}

bool
type_get_value_type (Type::Kind type)
{
	return Type::Find (type)->value_type;
}

DependencyObject *
type_create_instance (Type *type)
{
        return type->CreateInstance ();
}

DependencyObject *
type_create_instance_from_kind (Type::Kind kind)
{
        Type *t = Type::Find (kind);

        if (t == NULL) {
                g_warning ("Unable to create instance of type %d. Type not found.", kind);
                return NULL;
        }

        return t->CreateInstance ();
}


static void 
types_init_manually (void)
{
	// Put types that does not inherit from DependencyObject here (manually)

	//Type::RegisterType ("Invalid", Type::INVALID, Value::INVALID);
	Type::RegisterType ("bool", Type::BOOL, true);
	Type::RegisterType ("double", Type::DOUBLE, true);
	Type::RegisterType ("uint64", Type::UINT64, true);
	Type::RegisterType ("int", Type::INT32, true);
	Type::RegisterType ("string", Type::STRING, false);
	Type::RegisterType ("Color", Type::COLOR, true);
	Type::RegisterType ("Point", Type::POINT, true);
	Type::RegisterType ("Rect", Type::RECT, true);
	Type::RegisterType ("RepeatBehaviour", Type::REPEATBEHAVIOR, true);
	Type::RegisterType ("Duration", Type::DURATION, true);
	Type::RegisterType ("int64", Type::INT64, true);
	Type::RegisterType ("TimeSpan", Type::TIMESPAN, true);
	Type::RegisterType ("KeyTime", Type::KEYTIME, true);
	Type::RegisterType ("double*", Type::DOUBLE_ARRAY, false);
	Type::RegisterType ("Point*", Type::POINT_ARRAY, false);
	Type::RegisterType ("EventObject", Type::EVENTOBJECT, false);
	Type::RegisterType ("TimeSource", Type::TIMESOURCE, Type::EVENTOBJECT);
	Type::RegisterType ("ManualTimeSource", Type::MANUALTIMESOURCE, Type::TIMESOURCE);
	Type::RegisterType ("SystemTimeSource", Type::SYSTEMTIMESOURCE, Type::TIMESOURCE);
	Type::RegisterType ("TimeManager", Type::TIMEMANAGER, Type::EVENTOBJECT);
	Type::RegisterType ("Surface", Type::SURFACE, Type::EVENTOBJECT);
#if 0 && DEBUG
	for (int i = 1; i < Type::LASTTYPE; i++) {
		if (Type::types [i] != NULL)
			continue;

		if (i > 0 && Type::types [i - 1] != NULL)
			printf ("Type %i is not initialized (previous type in enum is '%s')\n", i, Type::types [i - 1]->name);
		else
			printf ("Type %i is not initialized\n", i);
	}
#endif
}

static void 
types_init_register_events (void)
{
	Type* t;

	t = Type::Find (Type::EVENTOBJECT);
	t->RegisterEvent ("Destroyed");

	t = Type::Find (Type::STORYBOARD);
	t->RegisterEvent ("Completed");

	t = Type::Find (Type::TIMEMANAGER);
	t->RegisterEvent ("update-input");
	t->RegisterEvent ("render");

	t = Type::Find (Type::TIMESOURCE);
	t->RegisterEvent ("Tick");

	t = Type::Find (Type::CLOCK);
	t->RegisterEvent ("CurrentTimeInvalidated");
	t->RegisterEvent ("CurrentStateInvalidated");
	t->RegisterEvent ("CurrentGlobalSpeedInvalidated");
	t->RegisterEvent ("Completed");

	t = Type::Find (Type::DOWNLOADER);
	t->RegisterEvent ("Completed");
	t->RegisterEvent ("DownloadProgressChanged");
	t->RegisterEvent ("DownloadFailed");

	t = Type::Find (Type::MEDIABASE);
	t->RegisterEvent ("DownloadProgressChanged");

	t = Type::Find (Type::MEDIAELEMENT);
	t->RegisterEvent ("BufferingProgressChanged");
	t->RegisterEvent ("CurrentStateChanged");
	t->RegisterEvent ("MarkerReached");
	t->RegisterEvent ("MediaEnded");
	t->RegisterEvent ("MediaFailed");
	t->RegisterEvent ("MediaOpened");

	t = Type::Find (Type::IMAGE);
	t->RegisterEvent ("ImageFailed");

	t = Type::Find (Type::IMAGEBRUSH);
	t->RegisterEvent ("DownloadProgressChanged");
	t->RegisterEvent ("ImageFailed");

	t = Type::Find(Type::SURFACE);
	t->RegisterEvent ("Resize");
	t->RegisterEvent ("FullScreenChange");
	t->RegisterEvent ("Error");

	t = Type::Find(Type::UIELEMENT);
	t->RegisterEvent ("Loaded");
	t->RegisterEvent ("MouseMove");
	t->RegisterEvent ("MouseLeftButtonDown");
	t->RegisterEvent ("MouseLeftButtonUp");
	t->RegisterEvent ("KeyDown");
	t->RegisterEvent ("KeyUp");
	t->RegisterEvent ("MouseEnter");
	t->RegisterEvent ("MouseLeave");
	t->RegisterEvent ("Invalidated");
	t->RegisterEvent ("GotFocus");
	t->RegisterEvent ("LostFocus");

	t = Type::Find(Type::COLLECTION);
	t->RegisterEvent ("__moonlight_CollectionChanged");
}

//
// The generated code will be put at the end of the file
//
// We are currently generating:
//	- types_init (), initializes all types that inherit from DependencyObject


void
types_init (void)
{
	if (types_initialized)
		return;
	types_initialized = true;


	Type::RegisterType ("DependencyObject", Type::DEPENDENCY_OBJECT, Type::EVENTOBJECT, NULL, NULL);
	Type::RegisterType ("Animation", Type::ANIMATION, Type::TIMELINE, NULL, NULL);
	Type::RegisterType ("AnimationClock", Type::ANIMATIONCLOCK, Type::CLOCK, NULL, NULL);
	Type::RegisterType ("ArcSegment", Type::ARCSEGMENT, Type::PATHSEGMENT, (create_inst_func *) arc_segment_new, NULL);
	Type::RegisterType ("BeginStoryboard", Type::BEGINSTORYBOARD, Type::TRIGGERACTION, (create_inst_func *) begin_storyboard_new, "Storyboard");
	Type::RegisterType ("BezierSegment", Type::BEZIERSEGMENT, Type::PATHSEGMENT, (create_inst_func *) bezier_segment_new, NULL);
	Type::RegisterType ("Brush", Type::BRUSH, Type::DEPENDENCY_OBJECT, (create_inst_func *) brush_new, NULL);
	Type::RegisterType ("Canvas", Type::CANVAS, Type::PANEL, (create_inst_func *) canvas_new, NULL);
	Type::RegisterType ("ChangeEventArgs", Type::CHANGEEVENTARGS, Type::EVENTARGS, NULL, NULL);
	Type::RegisterType ("Clock", Type::CLOCK, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("ClockGroup", Type::CLOCKGROUP, Type::CLOCK, NULL, NULL);
	Type::RegisterType ("Collection", Type::COLLECTION, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("ColorAnimation", Type::COLORANIMATION, Type::ANIMATION, (create_inst_func *) color_animation_new, NULL);
	Type::RegisterType ("ColorAnimationUsingKeyFrames", Type::COLORANIMATIONUSINGKEYFRAMES, Type::COLORANIMATION, (create_inst_func *) color_animation_using_key_frames_new, NULL);
	Type::RegisterType ("ColorKeyFrame", Type::COLORKEYFRAME, Type::KEYFRAME, (create_inst_func *) color_key_frame_new, NULL);
	Type::RegisterType ("ColorKeyFrameCollection", Type::COLORKEYFRAME_COLLECTION, Type::KEYFRAME_COLLECTION, (create_inst_func *) color_key_frame_collection_new, NULL);
	Type::RegisterType ("Control", Type::CONTROL, Type::FRAMEWORKELEMENT, (create_inst_func *) control_new, NULL);
	Type::RegisterType ("DiscreteColorKeyFrame", Type::DISCRETECOLORKEYFRAME, Type::COLORKEYFRAME, (create_inst_func *) discrete_color_key_frame_new, NULL);
	Type::RegisterType ("DiscreteDoubleKeyFrame", Type::DISCRETEDOUBLEKEYFRAME, Type::DOUBLEKEYFRAME, (create_inst_func *) discrete_double_key_frame_new, NULL);
	Type::RegisterType ("DiscretePointKeyFrame", Type::DISCRETEPOINTKEYFRAME, Type::POINTKEYFRAME, (create_inst_func *) discrete_point_key_frame_new, NULL);
	Type::RegisterType ("DoubleAnimation", Type::DOUBLEANIMATION, Type::ANIMATION, (create_inst_func *) double_animation_new, NULL);
	Type::RegisterType ("DoubleAnimationUsingKeyFrames", Type::DOUBLEANIMATIONUSINGKEYFRAMES, Type::DOUBLEANIMATION, (create_inst_func *) double_animation_using_key_frames_new, "KeyFrames");
	Type::RegisterType ("DoubleKeyFrame", Type::DOUBLEKEYFRAME, Type::KEYFRAME, (create_inst_func *) double_key_frame_new, NULL);
	Type::RegisterType ("DoubleKeyFrameCollection", Type::DOUBLEKEYFRAME_COLLECTION, Type::KEYFRAME_COLLECTION, (create_inst_func *) double_key_frame_collection_new, NULL);
	Type::RegisterType ("Downloader", Type::DOWNLOADER, Type::DEPENDENCY_OBJECT, (create_inst_func *) downloader_new, NULL);
	Type::RegisterType ("DrawingAttributes", Type::DRAWINGATTRIBUTES, Type::DEPENDENCY_OBJECT, (create_inst_func *) drawing_attributes_new, NULL);
	Type::RegisterType ("Ellipse", Type::ELLIPSE, Type::SHAPE, (create_inst_func *) ellipse_new, NULL);
	Type::RegisterType ("EllipseGeometry", Type::ELLIPSEGEOMETRY, Type::GEOMETRY, (create_inst_func *) ellipse_geometry_new, NULL);
	Type::RegisterType ("ErrorEventArgs", Type::ERROREVENTARGS, Type::EVENTARGS, NULL, NULL);
	Type::RegisterType ("EventArgs", Type::EVENTARGS, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("EventTrigger", Type::EVENTTRIGGER, Type::DEPENDENCY_OBJECT, (create_inst_func *) event_trigger_new, "Actions");
	Type::RegisterType ("FrameworkElement", Type::FRAMEWORKELEMENT, Type::UIELEMENT, (create_inst_func *) framework_element_new, NULL);
	Type::RegisterType ("Geometry", Type::GEOMETRY, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("GeometryCollection", Type::GEOMETRY_COLLECTION, Type::COLLECTION, (create_inst_func *) geometry_collection_new, NULL);
	Type::RegisterType ("GeometryGroup", Type::GEOMETRYGROUP, Type::GEOMETRY, NULL, "Children");
	Type::RegisterType ("Glyphs", Type::GLYPHS, Type::FRAMEWORKELEMENT, (create_inst_func *) glyphs_new, NULL);
	Type::RegisterType ("GradientBrush", Type::GRADIENTBRUSH, Type::BRUSH, (create_inst_func *) gradient_brush_new, "GradientStops");
	Type::RegisterType ("GradientStop", Type::GRADIENTSTOP, Type::DEPENDENCY_OBJECT, (create_inst_func *) gradient_stop_new, NULL);
	Type::RegisterType ("GradientStopCollection", Type::GRADIENTSTOP_COLLECTION, Type::COLLECTION, (create_inst_func *) gradient_stop_collection_new, NULL);
	Type::RegisterType ("Image", Type::IMAGE, Type::MEDIABASE, (create_inst_func *) image_new, NULL);
	Type::RegisterType ("ImageBrush", Type::IMAGEBRUSH, Type::TILEBRUSH, (create_inst_func *) image_brush_new, NULL);
	Type::RegisterType ("ImageErrorEventArgs", Type::IMAGEERROREVENTARGS, Type::ERROREVENTARGS, NULL, NULL);
	Type::RegisterType ("InkPresenter", Type::INKPRESENTER, Type::CANVAS, (create_inst_func *) ink_presenter_new, NULL);
	Type::RegisterType ("Inline", Type::INLINE, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("Inlines", Type::INLINES, Type::COLLECTION, (create_inst_func *) inlines_new, NULL);
	Type::RegisterType ("KeyboardEventArgs", Type::KEYBOARDEVENTARGS, Type::EVENTARGS, NULL, NULL);
	Type::RegisterType ("KeyFrame", Type::KEYFRAME, Type::DEPENDENCY_OBJECT, (create_inst_func *) key_frame_new, NULL);
	Type::RegisterType ("KeyFrameCollection", Type::KEYFRAME_COLLECTION, Type::COLLECTION, NULL, NULL);
	Type::RegisterType ("KeySpline", Type::KEYSPLINE, Type::DEPENDENCY_OBJECT, (create_inst_func *) key_spline_new, NULL);
	Type::RegisterType ("Line", Type::LINE, Type::SHAPE, (create_inst_func *) line_new, NULL);
	Type::RegisterType ("LinearColorKeyFrame", Type::LINEARCOLORKEYFRAME, Type::COLORKEYFRAME, (create_inst_func *) linear_color_key_frame_new, NULL);
	Type::RegisterType ("LinearDoubleKeyFrame", Type::LINEARDOUBLEKEYFRAME, Type::DOUBLEKEYFRAME, (create_inst_func *) linear_double_key_frame_new, NULL);
	Type::RegisterType ("LinearGradientBrush", Type::LINEARGRADIENTBRUSH, Type::GRADIENTBRUSH, (create_inst_func *) linear_gradient_brush_new, NULL);
	Type::RegisterType ("LinearPointKeyFrame", Type::LINEARPOINTKEYFRAME, Type::POINTKEYFRAME, (create_inst_func *) linear_point_key_frame_new, NULL);
	Type::RegisterType ("LineBreak", Type::LINEBREAK, Type::INLINE, (create_inst_func *) line_break_new, NULL);
	Type::RegisterType ("LineGeometry", Type::LINEGEOMETRY, Type::GEOMETRY, (create_inst_func *) line_geometry_new, NULL);
	Type::RegisterType ("LineSegment", Type::LINESEGMENT, Type::PATHSEGMENT, (create_inst_func *) line_segment_new, NULL);
	Type::RegisterType ("MarkerReachedEventArgs", Type::MARKERREACHEDEVENTARGS, Type::EVENTARGS, NULL, NULL);
	Type::RegisterType ("Matrix", Type::MATRIX, Type::DEPENDENCY_OBJECT, (create_inst_func *) matrix_new, NULL);
	Type::RegisterType ("MatrixTransform", Type::MATRIXTRANSFORM, Type::TRANSFORM, (create_inst_func *) matrix_transform_new, NULL);
	Type::RegisterType ("MediaAttribute", Type::MEDIAATTRIBUTE, Type::DEPENDENCY_OBJECT, (create_inst_func *) media_attribute_new, NULL);
	Type::RegisterType ("MediaAttributeCollection", Type::MEDIAATTRIBUTE_COLLECTION, Type::COLLECTION, (create_inst_func *) media_attribute_collection_new, NULL);
	Type::RegisterType ("MediaBase", Type::MEDIABASE, Type::FRAMEWORKELEMENT, (create_inst_func *) media_base_new, NULL);
	Type::RegisterType ("MediaElement", Type::MEDIAELEMENT, Type::MEDIABASE, (create_inst_func *) media_element_new, NULL);
	Type::RegisterType ("MediaErrorEventArgs", Type::MEDIAERROREVENTARGS, Type::ERROREVENTARGS, NULL, NULL);
	Type::RegisterType ("MouseEventArgs", Type::MOUSEEVENTARGS, Type::EVENTARGS, NULL, NULL);
	Type::RegisterType ("NameScope", Type::NAMESCOPE, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("Panel", Type::PANEL, Type::FRAMEWORKELEMENT, (create_inst_func *) panel_new, "Children");
	Type::RegisterType ("ParallelTimeline", Type::PARALLELTIMELINE, Type::TIMELINEGROUP, (create_inst_func *) parallel_timeline_new, NULL);
	Type::RegisterType ("ParserErrorEventArgs", Type::PARSERERROREVENTARGS, Type::ERROREVENTARGS, NULL, NULL);
	Type::RegisterType ("Path", Type::PATH, Type::SHAPE, (create_inst_func *) path_new, NULL);
	Type::RegisterType ("PathFigure", Type::PATHFIGURE, Type::DEPENDENCY_OBJECT, (create_inst_func *) path_figure_new, "Segments");
	Type::RegisterType ("PathFigureCollection", Type::PATHFIGURE_COLLECTION, Type::COLLECTION, (create_inst_func *) path_figure_collection_new, NULL);
	Type::RegisterType ("PathGeometry", Type::PATHGEOMETRY, Type::GEOMETRY, NULL, "Figures");
	Type::RegisterType ("PathSegment", Type::PATHSEGMENT, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("PathSegmentCollection", Type::PATHSEGMENT_COLLECTION, Type::COLLECTION, (create_inst_func *) path_segment_collection_new, NULL);
	Type::RegisterType ("PointAnimation", Type::POINTANIMATION, Type::ANIMATION, (create_inst_func *) point_animation_new, NULL);
	Type::RegisterType ("PointAnimationUsingKeyFrames", Type::POINTANIMATIONUSINGKEYFRAMES, Type::POINTANIMATION, (create_inst_func *) point_animation_using_key_frames_new, "KeyFrames");
	Type::RegisterType ("PointKeyFrame", Type::POINTKEYFRAME, Type::KEYFRAME, (create_inst_func *) point_key_frame_new, NULL);
	Type::RegisterType ("PointKeyFrameCollection", Type::POINTKEYFRAME_COLLECTION, Type::KEYFRAME_COLLECTION, (create_inst_func *) point_key_frame_collection_new, NULL);
	Type::RegisterType ("PolyBezierSegment", Type::POLYBEZIERSEGMENT, Type::PATHSEGMENT, (create_inst_func *) poly_bezier_segment_new, NULL);
	Type::RegisterType ("Polygon", Type::POLYGON, Type::SHAPE, NULL, NULL);
	Type::RegisterType ("Polyline", Type::POLYLINE, Type::SHAPE, NULL, NULL);
	Type::RegisterType ("PolyLineSegment", Type::POLYLINESEGMENT, Type::PATHSEGMENT, (create_inst_func *) poly_line_segment_new, NULL);
	Type::RegisterType ("PolyQuadraticBezierSegment", Type::POLYQUADRATICBEZIERSEGMENT, Type::PATHSEGMENT, (create_inst_func *) poly_quadratic_bezier_segment_new, NULL);
	Type::RegisterType ("QuadraticBezierSegment", Type::QUADRATICBEZIERSEGMENT, Type::PATHSEGMENT, (create_inst_func *) quadratic_bezier_segment_new, NULL);
	Type::RegisterType ("RadialGradientBrush", Type::RADIALGRADIENTBRUSH, Type::GRADIENTBRUSH, (create_inst_func *) radial_gradient_brush_new, NULL);
	Type::RegisterType ("Rectangle", Type::RECTANGLE, Type::SHAPE, (create_inst_func *) rectangle_new, NULL);
	Type::RegisterType ("RectangleGeometry", Type::RECTANGLEGEOMETRY, Type::GEOMETRY, (create_inst_func *) rectangle_geometry_new, NULL);
	Type::RegisterType ("ResourceDictionary", Type::RESOURCE_DICTIONARY, Type::COLLECTION, (create_inst_func *) resource_dictionary_new, NULL);
	Type::RegisterType ("RotateTransform", Type::ROTATETRANSFORM, Type::TRANSFORM, (create_inst_func *) rotate_transform_new, NULL);
	Type::RegisterType ("Run", Type::RUN, Type::INLINE, (create_inst_func *) run_new, "Text");
	Type::RegisterType ("ScaleTransform", Type::SCALETRANSFORM, Type::TRANSFORM, (create_inst_func *) scale_transform_new, NULL);
	Type::RegisterType ("Shape", Type::SHAPE, Type::FRAMEWORKELEMENT, NULL, NULL);
	Type::RegisterType ("SkewTransform", Type::SKEWTRANSFORM, Type::TRANSFORM, (create_inst_func *) skew_transform_new, NULL);
	Type::RegisterType ("SolidColorBrush", Type::SOLIDCOLORBRUSH, Type::BRUSH, (create_inst_func *) solid_color_brush_new, NULL);
	Type::RegisterType ("SplineColorKeyFrame", Type::SPLINECOLORKEYFRAME, Type::COLORKEYFRAME, (create_inst_func *) spline_color_key_frame_new, NULL);
	Type::RegisterType ("SplineDoubleKeyFrame", Type::SPLINEDOUBLEKEYFRAME, Type::DOUBLEKEYFRAME, (create_inst_func *) spline_double_key_frame_new, NULL);
	Type::RegisterType ("SplinePointKeyFrame", Type::SPLINEPOINTKEYFRAME, Type::POINTKEYFRAME, (create_inst_func *) spline_point_key_frame_new, NULL);
	Type::RegisterType ("Storyboard", Type::STORYBOARD, Type::PARALLELTIMELINE, (create_inst_func *) storyboard_new, "Children");
	Type::RegisterType ("Stroke", Type::STROKE, Type::DEPENDENCY_OBJECT, (create_inst_func *) stroke_new, NULL);
	Type::RegisterType ("StrokeCollection", Type::STROKE_COLLECTION, Type::COLLECTION, (create_inst_func *) stroke_collection_new, NULL);
	Type::RegisterType ("StylusInfo", Type::STYLUSINFO, Type::DEPENDENCY_OBJECT, (create_inst_func *) stylus_info_new, NULL);
	Type::RegisterType ("StylusPoint", Type::STYLUSPOINT, Type::DEPENDENCY_OBJECT, (create_inst_func *) stylus_point_new, NULL);
	Type::RegisterType ("StylusPointCollection", Type::STYLUSPOINT_COLLECTION, Type::COLLECTION, (create_inst_func *) stylus_point_collection_new, NULL);
	Type::RegisterType ("TextBlock", Type::TEXTBLOCK, Type::FRAMEWORKELEMENT, (create_inst_func *) text_block_new, "Inlines");
	Type::RegisterType ("TileBrush", Type::TILEBRUSH, Type::BRUSH, (create_inst_func *) tile_brush_new, NULL);
	Type::RegisterType ("Timeline", Type::TIMELINE, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("TimelineCollection", Type::TIMELINE_COLLECTION, Type::COLLECTION, (create_inst_func *) timeline_collection_new, NULL);
	Type::RegisterType ("TimelineGroup", Type::TIMELINEGROUP, Type::TIMELINE, (create_inst_func *) timeline_group_new, NULL);
	Type::RegisterType ("TimelineMarker", Type::TIMELINEMARKER, Type::DEPENDENCY_OBJECT, (create_inst_func *) timeline_marker_new, NULL);
	Type::RegisterType ("TimelineMarkerCollection", Type::TIMELINEMARKER_COLLECTION, Type::COLLECTION, (create_inst_func *) timeline_marker_collection_new, NULL);
	Type::RegisterType ("Transform", Type::TRANSFORM, Type::DEPENDENCY_OBJECT, (create_inst_func *) transform_new, NULL);
	Type::RegisterType ("TransformCollection", Type::TRANSFORM_COLLECTION, Type::COLLECTION, (create_inst_func *) transform_collection_new, NULL);
	Type::RegisterType ("TransformGroup", Type::TRANSFORMGROUP, Type::TRANSFORM, (create_inst_func *) transform_group_new, "Children");
	Type::RegisterType ("TranslateTransform", Type::TRANSLATETRANSFORM, Type::TRANSFORM, (create_inst_func *) translate_transform_new, NULL);
	Type::RegisterType ("TriggerAction", Type::TRIGGERACTION, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("TriggerActionCollection", Type::TRIGGERACTION_COLLECTION, Type::COLLECTION, (create_inst_func *) trigger_action_collection_new, NULL);
	Type::RegisterType ("TriggerCollection", Type::TRIGGER_COLLECTION, Type::COLLECTION, (create_inst_func *) trigger_collection_new, NULL);
	Type::RegisterType ("UIElement", Type::UIELEMENT, Type::VISUAL, (create_inst_func *) uielement_new, NULL);
	Type::RegisterType ("VideoBrush", Type::VIDEOBRUSH, Type::TILEBRUSH, (create_inst_func *) video_brush_new, NULL);
	Type::RegisterType ("Visual", Type::VISUAL, Type::DEPENDENCY_OBJECT, NULL, NULL);
	Type::RegisterType ("VisualBrush", Type::VISUALBRUSH, Type::TILEBRUSH, (create_inst_func *) visual_brush_new, NULL);
	Type::RegisterType ("VisualCollection", Type::VISUAL_COLLECTION, Type::COLLECTION, (create_inst_func *) visual_collection_new, NULL);
	types_init_manually ();
	types_init_register_events ();
}
