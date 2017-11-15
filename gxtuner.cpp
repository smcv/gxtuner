/*
 * Copyright (C) 2011 Hermann Meyer, Andreas Degert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * ---------------------------------------------------------------------------
 *
 *        file: gxtuner.cpp      guitar tuner for jack
 *
 * ----------------------------------------------------------------------------
 */

#include "./gxtuner.h"


#include <string.h> 
#include <math.h>
#include <stdlib.h>
#define P_(s) (s)   // FIXME -> gettext

enum {
    PROP_FREQ = 1,
    PROP_REFERENCE_PITCH = 2,
    PROP_MODE = 3,
};

static gboolean gtk_tuner_expose (GtkWidget *widget, cairo_t *cr);
static void draw_background(cairo_surface_t *surface_tuner);
static void gx_tuner_class_init (GxTunerClass *klass);
static void gx_tuner_base_class_finalize(GxTunerClass *klass);
static void gx_tuner_init(GxTuner *tuner);
static void gx_tuner_set_property(
    GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gx_tuner_get_property(
    GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static const int tuner_width = 100;
static const int tuner_height = 60;
static const double rect_width = 100;
static const double rect_height = 60;

static int cents = 0;
static float mini_cents = 0.0;

static const double dashline[] = {
    3.0                
};

static const double dashes[] = {
    0.0,                      /* ink  */
    rect_height,              /* skip */
    10.0,                     /* ink  */
    10.0                      /* skip */
};

static const double dash_ind[] = {
    0,                         /* ink  */
    14,                        /* skip */
    rect_height-18,            /* ink  */
    100.0                      /* skip */
};

static const double no_dash[] = {
    1000,                      /* ink  */
    0,                         /* skip */
    1000,                      /* ink  */
    0                          /* skip */
};


static void gx_tuner_get_preferred_size (GtkWidget *widget,
	GtkOrientation orientation, gint *minimal_size, gint *natural_size)
{
	if (orientation == GTK_ORIENTATION_HORIZONTAL)
	{
		*minimal_size = *natural_size = 100;
	}
	else
	{
		*minimal_size = *natural_size = 60;
	}
}

static void gx_tuner_get_preferred_width (
	GtkWidget *widget, gint *minimal_width, gint *natural_width)
{
	gx_tuner_get_preferred_size (widget,
		GTK_ORIENTATION_HORIZONTAL, minimal_width, natural_width);
}

static void gx_tuner_get_preferred_height (
	GtkWidget *widget, gint *minimal_height, gint *natural_height)
{
  gx_tuner_get_preferred_size (widget,
	GTK_ORIENTATION_VERTICAL, minimal_height, natural_height);
}


GType gx_tuner_get_type(void) {
    static GType tuner_type = 0;

    if (!tuner_type) {
        const GTypeInfo tuner_info = {
            sizeof (GxTunerClass),
            NULL,                /* base_class_init */
            (GBaseFinalizeFunc) gx_tuner_base_class_finalize,
            (GClassInitFunc) gx_tuner_class_init,
            NULL,                /* class_finalize */
            NULL,                /* class_data */
            sizeof (GxTuner),
            0,                    /* n_preallocs */
            (GInstanceInitFunc) gx_tuner_init,
            NULL,                /* value_table */
        };
        tuner_type = g_type_register_static(
            GTK_TYPE_DRAWING_AREA, "GxTuner", &tuner_info, (GTypeFlags)0);
    }
    return tuner_type;
}

static void gx_tuner_class_init(GxTunerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->draw = gtk_tuner_expose;
	widget_class->get_preferred_width = gx_tuner_get_preferred_width;
	widget_class->get_preferred_height = gx_tuner_get_preferred_height;
    gobject_class->set_property = gx_tuner_set_property;
    gobject_class->get_property = gx_tuner_get_property;
    g_object_class_install_property(
        gobject_class, PROP_FREQ, g_param_spec_double (
            "freq", P_("Frequency"),
            P_("The frequency for which tuning is displayed"),
            0.0, 1000.0, 0.0, G_PARAM_READWRITE));
    g_object_class_install_property(
        gobject_class, PROP_REFERENCE_PITCH, g_param_spec_double (
            "reference-pitch", P_("Reference Pitch"),
            P_("The frequency for which tuning is displayed"),
            400.0, 500.0, 440.0, G_PARAM_READWRITE));
    g_object_class_install_property(
        gobject_class, PROP_MODE, g_param_spec_int (
            "mode", P_("Tuning Mode"),
            P_("The Mode for which tuning is displayed"),
            0, 1, 0, G_PARAM_READWRITE));
    klass->surface_tuner = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, tuner_width*3., tuner_height*3.);
    g_assert(klass->surface_tuner != NULL);
    draw_background(klass->surface_tuner);
}

static void gx_tuner_base_class_finalize(GxTunerClass *klass) {
    if (klass->surface_tuner) {
        g_object_unref(klass->surface_tuner);
    }
}

static void gx_tuner_init (GxTuner *tuner) {
    g_assert(GX_IS_TUNER(tuner));
    tuner->freq = 0;
    tuner->reference_pitch = 440.0;
    tuner->mode = 0;
    tuner->scale_w = 1.;
    tuner->scale_h = 1.;
    //GtkWidget *widget = GTK_WIDGET(tuner);
}

void gx_tuner_set_freq(GxTuner *tuner, double freq) {
    g_assert(GX_IS_TUNER(tuner));
    if (tuner->freq != freq) {
        tuner->freq = freq;
        gtk_widget_queue_draw(GTK_WIDGET(tuner));
        g_object_notify(G_OBJECT(tuner), "freq");
    }
}

void gx_tuner_set_reference_pitch(GxTuner *tuner, double reference_pitch) {
    g_assert(GX_IS_TUNER(tuner));
    tuner->reference_pitch = reference_pitch;
    gtk_widget_queue_draw(GTK_WIDGET(tuner));
    g_object_notify(G_OBJECT(tuner), "reference-pitch");
}

void gx_tuner_set_mode(GxTuner *tuner, int mode) {
    g_assert(GX_IS_TUNER(tuner));
    tuner->mode = mode;
    gtk_widget_queue_draw(GTK_WIDGET(tuner));
    g_object_notify(G_OBJECT(tuner), "mode");
}

double gx_tuner_get_reference_pitch(GxTuner *tuner) {
    g_assert(GX_IS_TUNER(tuner));
    return tuner->reference_pitch;
}

GtkWidget *gx_tuner_new(void) {
    return (GtkWidget*)g_object_new(GX_TYPE_TUNER, NULL);
}

static void gx_tuner_set_property(GObject *object, guint prop_id,
                                      const GValue *value, GParamSpec *pspec) {
    GxTuner *tuner = GX_TUNER(object);

    switch(prop_id) {
    case PROP_FREQ:
        gx_tuner_set_freq(tuner, g_value_get_double(value));
        break;
    case PROP_REFERENCE_PITCH:
        gx_tuner_set_reference_pitch(tuner, g_value_get_double(value));
        break;
    case PROP_MODE:
        gx_tuner_set_mode(tuner, g_value_get_int(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void gx_tuner_get_property(GObject *object, guint prop_id,
                                      GValue *value, GParamSpec *pspec) {
    GxTuner *tuner = GX_TUNER(object);

    switch(prop_id) {
    case PROP_FREQ:
        g_value_set_double(value, tuner->freq);
        break;
    case PROP_REFERENCE_PITCH:
        g_value_set_double(value, tuner->reference_pitch);
        break;
    case PROP_MODE:
        g_value_set_int(value, tuner->mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static double log_scale(int cent, double set) {
    if (cent == 0) {
        return set;
    } else if (cent < 2 && cent > -2) { // 1 cent
        if (set<0.0) return set - 0.0155;
        else return set + 0.0155;
    } else if (cent < 3 && cent > -3) { // 2 cent
        if (set<0.0) return set - 0.0265;
        else return set + 0.0265;
    }else if (cent < 4 && cent > -4) { // 3 cent
        if (set<0.0) return set - 0.0355;
        else return set + 0.0355;
    } else if (cent < 5 && cent > -5) { // 4 cent
        if (set<0.0) return set - 0.0455;
        else return set + 0.0455;
    } else if (cent < 6 && cent > -6) { // 5 cent
        if (set<0.0) return set - 0.0435;
        else return set + 0.0435;
    } else if (cent < 7 && cent > -7) { // 6 cent
        if (set<0.0) return set - 0.0425;
        else return set + 0.0425;
    } else if (cent < 8 && cent > -8) { // 7 cent
        if (set<0.0) return set - 0.0415;
        else return set + 0.0415;
    } else if (cent < 9 && cent > -9) { // 8 cent
        if (set<0.0) return set - 0.0405;
        else return set + 0.0405;
    } else if (cent < 10 && cent > -10) { // 9 cent
        if (set<0.0) return set - 0.0385;
        else return set + 0.0385;
    } else if (cent < 11 && cent > -11) { // 10 cent
        if (set<0.0) return set - 0.0365;
        else return set + 0.0365;
    } else if (cent < 51 && cent > -51) { // < 50 cent
        return set + (0.4/cent);
    } else return set;
}

static void gx_tuner_triangle(cairo_t *cr, double posx, double posy, double width, double height)
{
	double h2 = height/2.0;
    cairo_move_to(cr, posx, posy-h2);
    if (width > 0) {
        cairo_curve_to(cr,posx, posy-h2, posx+10, posy, posx, posy+h2);
    } else {
        cairo_curve_to(cr,posx, posy-h2, posx-10, posy, posx, posy+h2);
    }
    cairo_curve_to(cr,posx, posy+h2, posx+width/2, posy+h2, posx+width, posy);
    cairo_curve_to(cr, posx+width, posy, posx+width/2, posy-h2, posx, posy-h2);
	cairo_fill(cr);
}

static void gx_tuner_strobe(cairo_t *cr, double x0, double y0, double cents) {
    static double move = 0;
    static double hold_l = 0;
    cairo_pattern_t *pat = cairo_pattern_create_linear (x0+50, y0,x0, y0);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REFLECT);
    cairo_pattern_add_color_stop_rgb (pat, 0, 0.1, 0.8, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.1, 0.1, 0.6, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.2, 0.3, 0.6, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.3, 0.4, 0.4, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 1, 0.8, 0.1, 0.1);
    cairo_set_source (cr, pat);

    if (abs(cents)>0) {
        if(hold_l>0 )
            hold_l -= 10.0 ;
        if (cents>0)
            move += pow(abs(cents),0.25);
        else if (cents<0)
            move -= pow(abs(cents),0.25);
    } else if (fabs(cents)>0.015){
        move += cents;
        if(hold_l>0 )
            hold_l -= 10.0 ;
    } else {
        move = 0;
        if(hold_l<rect_width/2 )
            hold_l += 10.0 ;
        else if(hold_l<rect_width/2 +1.5)
            hold_l = rect_width/2+1.5 ;
    }
    if(move<0)
        move = rect_width;
    else if (move>rect_width)
        move = 0;

    cairo_set_dash (cr, dashline, sizeof(dashline)/sizeof(dashline[0]), move);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr,x0+hold_l, y0+1);
    cairo_line_to(cr, x0+rect_width-hold_l , y0+1);
    cairo_stroke(cr);   
    cairo_pattern_destroy(pat);

}

// copy the following block and edit it to your needs to add a new tuning mode
static gboolean gtk_tuner_expose_diatonic(GtkWidget *widget, cairo_t *cr) {
    // Note names for display
    static const char* diatonic_note[7] = {"Do","Re","Mi","Fa","Sol","La ","Ti"};
    // Frequency Octave divider 
    float multiply = 1.0;
    // ratio 
    float percent = 0.0;
    // Note indicator
    int display_note = 0;
    // Octave names for display
    static const char* octave[9] = {"0","1","2","3","4","5","6","7"," "};
    // Octave indicator
    static int indicate_oc = 0;
    
    GxTuner *tuner = GX_TUNER(widget);
    // fetch widget size and location
    GtkAllocation *allocation = g_new0 (GtkAllocation, 1);
    gtk_widget_get_allocation(GTK_WIDGET(widget), allocation); 

    double x0      = (allocation->width - 100) * 0.5;
    double y0      = (allocation->height - 60) * 0.5;

    static double grow   = 0.;

    if(allocation->width > allocation->height +(10.*grow*3)) {
        grow = (allocation->height/60.)/10.;
    } else {
        grow =  (allocation->width/100.)/10.;
    }
    
    tuner->scale_h = (allocation->height/60.)/3.;
    tuner->scale_w =  (allocation->width/100.)/3.;
    // translate widget size to standart size
    cairo_translate(cr, -x0*tuner->scale_w, -y0*tuner->scale_h);
    cairo_scale(cr, tuner->scale_w, tuner->scale_h);
    cairo_set_source_surface(cr, GX_TUNER_CLASS(GTK_WIDGET_GET_CLASS(widget))->surface_tuner, x0, y0);
    cairo_paint (cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, -x0*tuner->scale_w*3., -y0*tuner->scale_h*3.);
    cairo_scale(cr, tuner->scale_w*3., tuner->scale_h*3.);
    
    
    // fetch Octave we are in, 
    float scale = -0.4;
    if (tuner->freq) {
        // this is the frequency we get from the pitch tracker
        float freq_is = tuner->freq;
        // Now we translate reference_pitch from LA to DO 
        // to get the reference pitch of the first Note in Octave 4
        // La hvae a ratio of 5/3 = 1.666666667
        // so divide the reference_pitch by 1.666666667 will
        // give us the reference pitch for DO in Octave 4
        float ref_c = tuner->reference_pitch / 1.666666667;
        // now check in which Octave we are with the tracked frequency
        // and set the Frequency Octave divider
        // ref_c is now the frequency of the first note in Octave, 
        // but we wont to check if the frequency is below the last Note in Octave
        // so, for example if freq_is is below ref_c we are in octave 3
        // if freq_is is below ref_c/2 we are in octave 2, etc.
    if (freq_is < (ref_c/8.0)-5.1 && freq_is >0.0) {
        indicate_oc = 0; // standart 27,5hz == 6,25%
        multiply = 16;
    } else if (freq_is < (ref_c/4.0)-5.1) {
        indicate_oc = 1; // standart 55hz == 12,5%
        multiply = 8;
    } else if (freq_is < (ref_c/2.0)-5.1) {
        indicate_oc = 2; // standart 110hz == 25&
        multiply = 4;
    } else if (freq_is < (ref_c)-5.1) {
        indicate_oc = 3; // standart 220hz == 50%
        multiply = 2;
    } else if (freq_is < (ref_c*2.0)-5.1) {
        indicate_oc = 4; // standart 440hz == 100%
        multiply = 1;
    } else if (freq_is < (ref_c*4.0)-5.1) {
        indicate_oc = 5; // standart 880hz == 200%
        multiply = 0.5;
    } else if (freq_is < (ref_c*8.0)-5.1) {
        indicate_oc = 6; // standart 1760hz == 400%
        multiply = 0.25;
    } else if (freq_is < (ref_c*16.0)-5.1) {
        indicate_oc = 7; // standart 3520hz == 800%
        multiply = 0.125;
    } else {
        indicate_oc = 8;
        multiply = 0.0625;
    }
    // we divide ref_c (refernce frequency of DO in Octave 4) 
    // with the multiply var, to get the frequency of DO in the Octave we are in.
    // then we devide the fetched frequency by the frequency of this (DO in this octave)
    // we get the ratio of the fetched frequency to the first Note in octave
    percent = (freq_is/(ref_c/multiply)) ;
    //fprintf(stderr, " percent == %f freq = %f ref_c = %f indicate_oc = %i \n", percent, freq_is, ref_c, indicate_oc);

    // now we chould check which ratio we have
    // we split the range between the nearest two ratios by half, 
    // so, that we know if we are below Re or above Do, for example
    // so, the ratio of DO is 1/1 = 1.0, the ratio of Re is 1/8 = 1.125
    // then we get 1.125 - 1 = 0.125 / 2 = 0.0625
    // so, if our ratio is below 1.0625, we have a frequency near Do.
    // we could display note 0. 
    // If it is above 1.0625 we check for the next ratio, usw.
    // If we've found the nearest ratio, we need to know how far we are away
    // from the wonted ratio, so we substract the wonted ratio from the fetched one.
    // for example if we get a ratio of 1.025 we are near DO, so we substarct
    // the ratio of DO from "percent" means 1.025 - 1.0 = 0.025 
    // by divide to the half we get the scale factor for display cents. 
    if (percent < 1.06) { //Do
        display_note = 0;
        scale = ((percent-1.0))/2.0;
    } else if (percent < 1.18) { // Re
        display_note = 1;
        scale = ((percent-1.125))/2.0;
    } else if (percent < 1.29) { // Mi
        display_note = 2;
        scale = ((percent-1.25))/2.0;
    } else if (percent < 1.42) { // Fa
        display_note = 3;
        scale = ((percent-1.3333))/2.0;
    } else if (percent < 1.58){ // Sol
        display_note = 4;
        scale = ((percent-1.5))/2.0;
    } else if (percent < 1.77) { // La
        display_note = 5;
        scale = ((percent-1.6667))/2.0;
    } else if (percent < 1.94) { // Ti
        display_note = 6;
        scale = ((percent-1.875))/2.0;
    }
        // display note
        cairo_set_source_rgba(cr, fabsf(scale)*3.0, 1-fabsf(scale)*3.0, 0.2,1-fabsf(scale)*2);
        cairo_set_font_size(cr, 18.0);
        cairo_move_to(cr,x0+50 -9 , y0+30 +9 );
        cairo_show_text(cr, diatonic_note[display_note]);
        cairo_set_font_size(cr, 8.0);
        cairo_move_to(cr,x0+54  , y0+30 +16 );
        cairo_show_text(cr, octave[indicate_oc]);
    }

    // display frequency
    char s[10];
    snprintf(s, sizeof(s), "%.1f Hz", tuner->freq);
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.1);
    cairo_set_font_size (cr, 7.5);
    cairo_text_extents_t ex;
    cairo_text_extents(cr, s, &ex);
    cairo_move_to (cr, x0+98-ex.width, y0+58);
    cairo_show_text(cr, s);
    // display cent
    if(scale>-0.4) {
        if(scale>0.004) {
            // here we translate the scale factor to cents and display them
            cents = static_cast<int>((floorf(scale * 10000) / 50));
            snprintf(s, sizeof(s), "+%i", cents);
            cairo_set_source_rgb (cr, 0.05, 0.5+0.022* abs(cents), 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            cairo_set_source_rgb (cr, 0.5+ 0.022* abs(cents), 0.35, 0.1);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            gx_tuner_strobe(cr, x0, y0, static_cast<double>(cents));
        } else if(scale<-0.004) {
            cents = static_cast<int>((ceil(scale * 10000) / 50));
            snprintf(s, sizeof(s), "%i", cents);
            cairo_set_source_rgb (cr, 0.05, 0.5+0.022* abs(cents), 0.1);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            cairo_set_source_rgb (cr, 0.5+ 0.022* abs(cents), 0.35, 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            gx_tuner_strobe(cr, x0, y0, static_cast<double>(cents));
        } else {
            cents = static_cast<int>((ceil(scale * 10000) / 50));
            mini_cents = (scale * 10000) / 50;
            if (mini_cents<0)
                snprintf(s, sizeof(s), "%.2f", mini_cents);
            else
                snprintf(s, sizeof(s), "+%.2f", mini_cents);
            cairo_set_source_rgb (cr, 0.05* abs(cents), 0.5, 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            gx_tuner_strobe(cr, x0, y0, mini_cents);
        }
    } else {
        cents = 100;
        snprintf(s, sizeof(s), "+ - cent");
    }    
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.1);
    cairo_set_font_size (cr, 6.0);
    cairo_text_extents(cr, s, &ex);
    cairo_move_to (cr, x0+28-ex.width, y0+58);
    cairo_show_text(cr, s);

    double ux=2., uy=2.;
    cairo_device_to_user_distance (cr, &ux, &uy);
    if (ux < uy)
        ux = uy;
    cairo_set_line_width (cr, ux + grow);

    // indicator (line)
    cairo_move_to(cr,x0+50, y0+rect_height+5);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_dash (cr, dash_ind, sizeof(dash_ind)/sizeof(dash_ind[0]), 1);
    cairo_line_to(cr, (log_scale(cents, scale)*2*rect_width)+x0+50, y0+(scale*scale*30)+2);
    cairo_set_source_rgb(cr,  0.5, 0.1, 0.1);
    cairo_stroke(cr);   

    g_free (allocation); 
    return FALSE;
}

static gboolean gtk_tuner_expose_shruti(GtkWidget *widget, cairo_t *cr) {
    static const char* shruti_note[22] = {"S ","r1","r2","R1","R2","g1","g2","G1","G2","M1","M2","m1","m2","P ","d1","d2","D1","D2","n1","n2","N1","N2"};
    float multiply = 1.0;
    float percent = 0.0;
    int display_note = 0;
    static const char* octave[9] = {"0","1","2","3","4","5","6","7"," "};
    static int indicate_oc = 0;
    
    GxTuner *tuner = GX_TUNER(widget);
    
    GtkAllocation *allocation = g_new0 (GtkAllocation, 1);
    gtk_widget_get_allocation(GTK_WIDGET(widget), allocation); 

    double x0      = (allocation->width - 100) * 0.5;
    double y0      = (allocation->height - 60) * 0.5;

    static double grow   = 0.;

    if(allocation->width > allocation->height +(10.*grow*3)) {
        grow = (allocation->height/60.)/10.;
    } else {
        grow =  (allocation->width/100.)/10.;
    }
    
    tuner->scale_h = (allocation->height/60.)/3.;
    tuner->scale_w =  (allocation->width/100.)/3.;
    
    cairo_translate(cr, -x0*tuner->scale_w, -y0*tuner->scale_h);
    cairo_scale(cr, tuner->scale_w, tuner->scale_h);
    cairo_set_source_surface(cr, GX_TUNER_CLASS(GTK_WIDGET_GET_CLASS(widget))->surface_tuner, x0, y0);
    cairo_paint (cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, -x0*tuner->scale_w*3., -y0*tuner->scale_h*3.);
    cairo_scale(cr, tuner->scale_w*3., tuner->scale_h*3.);
    
    
    
    float scale = -0.4;
    if (tuner->freq) {
        float freq_is = tuner->freq;
    if (freq_is < (tuner->reference_pitch/8.0)-5.1 && freq_is >0.0) {
        indicate_oc = 0; // standart 27,5hz == 6,25%
        multiply = 16;
    } else if (freq_is < (tuner->reference_pitch/4.0)-5.1) {
        indicate_oc = 1; // standart 55hz == 12,5%
        multiply = 8;
    } else if (freq_is < (tuner->reference_pitch/2.0)-5.1) {
        indicate_oc = 2; // standart 110hz == 25&
        multiply = 4;
    } else if (freq_is < (tuner->reference_pitch)-5.1) {
        indicate_oc = 3; // standart 220hz == 50%
        multiply = 2;
    } else if (freq_is < (tuner->reference_pitch*2.0)-5.1) {
        indicate_oc = 4; // standart 440hz == 100%
        multiply = 1;
    } else if (freq_is < (tuner->reference_pitch*4.0)-5.1) {
        indicate_oc = 5; // standart 880hz == 200%
        multiply = 0.5;
    } else if (freq_is < (tuner->reference_pitch*8.0)-5.1) {
        indicate_oc = 6; // standart 1760hz == 400%
        multiply = 0.25;
    } else if (freq_is < (tuner->reference_pitch*16.0)-5.1) {
        indicate_oc = 7; // standart 3520hz == 800%
        multiply = 0.125;
    } else {
        indicate_oc = 8;
        multiply = 0.0625;
    }

    percent = ((tuner->reference_pitch/multiply)/freq_is) * 100.0;

    if (percent > 97.46) {
        display_note = 0;
        scale = ((percent-100.00)/100.0)/2.0;
    } else if (percent > 94.34) {
        display_note = 1;
        scale = ((percent-94.92)/100.0)/2.0;
    } else if (percent > 91.86) {
        display_note = 2;
        scale = ((percent-93.75)/100.0)/2.0;
    } else if (percent > 89.5) {
        display_note = 3;
        scale = ((percent-90.00)/100.0)/2.0;
    } else if (percent > 86.62){
        display_note = 4;
        scale = ((percent-88.88)/100.0)/2.0;
    } else if (percent > 83.85) {
        display_note = 5;
        scale = ((percent-84.37)/100.0)/2.0;
    } else if (percent > 81.67) {
        display_note = 6;
        scale = ((percent-83.33)/100.0)/2.0;
    } else if (percent > 79.51) {
        display_note = 7;
        scale = ((percent-80.00)/100.0)/2.0;
    } else if (percent > 77.01) {
        display_note = 8;
        scale = ((percent-79.01)/100.0)/2.0;
    } else if (percent > 74.54) {
        display_note = 9;
        scale = ((percent-75.00)/100.0)/2.0;
    } else if (percent > 72.59) {
        display_note = 10;
        scale = ((percent-74.07)/100.0)/2.0;
    } else if (percent > 70.67) {
        display_note = 11;
        scale = ((percent-71.11)/100.0)/2.0;
    } else if (percent > 68.45) {
        display_note = 12;
        scale = ((percent-70.23)/100.0)/2.0;
    } else if (percent > 64.97) {
        display_note = 13;
        scale = ((percent-66.66)/100.0)/2.0;
    } else if (percent > 62.89) {
        display_note = 14;
        scale = ((percent-63.28)/100.0)/2.0;
    } else if (percent > 61.25) {
        display_note = 15;
        scale = ((percent-62.50)/100.0)/2.0;
    } else if (percent > 59.63) {
        display_note = 16;
        scale = ((percent-60.00)/100.0)/2.0;
    } else if (percent > 57.75) {
        display_note = 17;
        scale = ((percent-59.25)/100.0)/2.0;
    } else if (percent > 55.90) {
        display_note = 18;
        scale = ((percent-56.25)/100.0)/2.0;
    } else if (percent > 54.44) {
        display_note = 19;
        scale = ((percent-55.55)/100.0)/2.0;
    } else if (percent > 53.00) {
        display_note = 20;
        scale = ((percent-53.33)/100.0)/2.0;
    } else if (percent > 51.34) {
        display_note = 21;
        scale = ((percent-52.67)/100.0)/2.0;
    }
        // display note
        cairo_set_source_rgba(cr, fabsf(scale)*3.0, 1-fabsf(scale)*3.0, 0.2,1-fabsf(scale)*2);
        cairo_set_font_size(cr, 18.0);
        cairo_move_to(cr,x0+50 -9 , y0+30 +9 );
        cairo_show_text(cr, shruti_note[display_note]);
        cairo_set_font_size(cr, 8.0);
        cairo_move_to(cr,x0+54  , y0+30 +16 );
        cairo_show_text(cr, octave[indicate_oc]);
    }

    // display frequency
    char s[10];
    snprintf(s, sizeof(s), "%.1f Hz", tuner->freq);
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.1);
    cairo_set_font_size (cr, 7.5);
    cairo_text_extents_t ex;
    cairo_text_extents(cr, s, &ex);
    cairo_move_to (cr, x0+98-ex.width, y0+58);
    cairo_show_text(cr, s);
    // display cent
    if(scale>-0.4) {
        if(scale>0.004) {
            cents = static_cast<int>((floorf(scale * 10000) / 50));
            snprintf(s, sizeof(s), "+%i", cents);
            cairo_set_source_rgb (cr, 0.05, 0.5+0.022* abs(cents), 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            cairo_set_source_rgb (cr, 0.5+ 0.022* abs(cents), 0.35, 0.1);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            gx_tuner_strobe(cr, x0, y0, static_cast<double>(cents));
        } else if(scale<-0.004) {
            cents = static_cast<int>((ceil(scale * 10000) / 50));
            snprintf(s, sizeof(s), "%i", cents);
            cairo_set_source_rgb (cr, 0.05, 0.5+0.022* abs(cents), 0.1);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            cairo_set_source_rgb (cr, 0.5+ 0.022* abs(cents), 0.35, 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            gx_tuner_strobe(cr, x0, y0, static_cast<double>(cents));
        } else {
            cents = static_cast<int>((ceil(scale * 10000) / 50));
            mini_cents = (scale * 10000) / 50;
            if (mini_cents<0)
                snprintf(s, sizeof(s), "%.2f", mini_cents);
            else
                snprintf(s, sizeof(s), "+%.2f", mini_cents);
            cairo_set_source_rgb (cr, 0.05* abs(cents), 0.5, 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            gx_tuner_strobe(cr, x0, y0, mini_cents);
        }
    } else {
        cents = 100;
        snprintf(s, sizeof(s), "+ - cent");
    }    
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.1);
    cairo_set_font_size (cr, 6.0);
    cairo_text_extents(cr, s, &ex);
    cairo_move_to (cr, x0+28-ex.width, y0+58);
    cairo_show_text(cr, s);

    double ux=2., uy=2.;
    cairo_device_to_user_distance (cr, &ux, &uy);
    if (ux < uy)
        ux = uy;
    cairo_set_line_width (cr, ux + grow);

    // indicator (line)
    cairo_move_to(cr,x0+50, y0+rect_height+5);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_dash (cr, dash_ind, sizeof(dash_ind)/sizeof(dash_ind[0]), 1);
    cairo_line_to(cr, (log_scale(cents, scale)*2*rect_width)+x0+50, y0+(scale*scale*30)+2);
    cairo_set_source_rgb(cr,  0.5, 0.1, 0.1);
    cairo_stroke(cr);   

    g_free (allocation); 
    return FALSE;
}

static gboolean gtk_tuner_expose (GtkWidget *widget, cairo_t *cr) {
    GxTuner *tuner = GX_TUNER(widget);
    // here we check in which mode we are add your mode here.
    if (tuner->mode == 1) {
        if (!gtk_tuner_expose_shruti (widget, cr)) return FALSE;
    } else if (tuner->mode == 2) {
        if (!gtk_tuner_expose_diatonic (widget, cr)) return FALSE;
    }
    static const char* note[12] = {"A ","A#","B ","C ","C#","D ","D#","E ","F ","F#","G ","G#"};
    static const char* octave[9] = {"0","1","2","3","4","5","6","7"," "};
    static int indicate_oc = 0;
    
    GtkAllocation *allocation = g_new0 (GtkAllocation, 1);
    gtk_widget_get_allocation(GTK_WIDGET(widget), allocation); 

    double x0      = (allocation->width - 100) * 0.5;
    double y0      = (allocation->height - 60) * 0.5;

    static double grow   = 0.;

    if(allocation->width > allocation->height +(10.*grow*3)) {
        grow = (allocation->height/60.)/10.;
    } else {
        grow =  (allocation->width/100.)/10.;
    }
    
    tuner->scale_h = (allocation->height/60.)/3.;
    tuner->scale_w =  (allocation->width/100.)/3.;
    
    cairo_translate(cr, -x0*tuner->scale_w, -y0*tuner->scale_h);
    cairo_scale(cr, tuner->scale_w, tuner->scale_h);
    cairo_set_source_surface(cr, GX_TUNER_CLASS(GTK_WIDGET_GET_CLASS(widget))->surface_tuner, x0, y0);
    cairo_paint (cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, -x0*tuner->scale_w*3., -y0*tuner->scale_h*3.);
    cairo_scale(cr, tuner->scale_w*3., tuner->scale_h*3.);
    
    float scale = -0.4;
    if (tuner->freq) {
        float freq_is = tuner->freq;
        float fvis = 12 * log2f(freq_is/tuner->reference_pitch);
        int vis = int(round(fvis));
        scale = (fvis-vis) / 2;
        vis = vis % 12;
        if (vis < 0) {
            vis += 12;
        }
        if (fabsf(scale) < 0.1) {
            if (freq_is < 31.78 && freq_is >0.0) {
                indicate_oc = 0;
            } else if (freq_is < 63.57) {
                indicate_oc = 1;
            } else if (freq_is < 127.14) {
                indicate_oc = 2;
            } else if (freq_is < 254.28) {
                indicate_oc = 3;
            } else if (freq_is < 509.44) {
                indicate_oc = 4;
            } else if (freq_is < 1017.35) {
                indicate_oc = 5;
            } else if (freq_is < 2034.26) {
                indicate_oc = 6;
            } else if (freq_is < 4068.54) {
                indicate_oc = 7;
            } else {
                indicate_oc = 8;
            }
        }else {
            indicate_oc = 8;
        }

        // display note
        cairo_set_source_rgba(cr, fabsf(scale)*3.0, 1-fabsf(scale)*3.0, 0.2,1-fabsf(scale)*2);
        cairo_set_font_size(cr, 18.0);
        cairo_move_to(cr,x0+50 -9 , y0+30 +9 );
        cairo_show_text(cr, note[vis]);
        cairo_set_font_size(cr, 8.0);
        cairo_move_to(cr,x0+54  , y0+30 +16 );
        cairo_show_text(cr, octave[indicate_oc]);
    }

    // display frequency
    char s[10];
    snprintf(s, sizeof(s), "%.1f Hz", tuner->freq);
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.1);
    cairo_set_font_size (cr, 7.5);
    cairo_text_extents_t ex;
    cairo_text_extents(cr, s, &ex);
    cairo_move_to (cr, x0+98-ex.width, y0+58);
    cairo_show_text(cr, s);
    // display cent
    if(scale>-0.4) {
        if(scale>0.004) {
            cents = static_cast<int>((floorf(scale * 10000) / 50));
            snprintf(s, sizeof(s), "+%i", cents);
            cairo_set_source_rgb (cr, 0.05, 0.5+0.022* abs(cents), 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            cairo_set_source_rgb (cr, 0.5+ 0.022* abs(cents), 0.35, 0.1);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            gx_tuner_strobe(cr, x0, y0, static_cast<double>(cents));
        } else if(scale<-0.004) {
            cents = static_cast<int>((ceil(scale * 10000) / 50));
            snprintf(s, sizeof(s), "%i", cents);
            cairo_set_source_rgb (cr, 0.05, 0.5+0.022* abs(cents), 0.1);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            cairo_set_source_rgb (cr, 0.5+ 0.022* abs(cents), 0.35, 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            gx_tuner_strobe(cr, x0, y0, static_cast<double>(cents));
        } else {
            cents = static_cast<int>((ceil(scale * 10000) / 50));
            mini_cents = (scale * 10000) / 50;
            if (mini_cents<0)
                snprintf(s, sizeof(s), "%.2f", mini_cents);
            else
                snprintf(s, sizeof(s), "+%.2f", mini_cents);
            cairo_set_source_rgb (cr, 0.05* abs(cents), 0.5, 0.1);
            gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
            gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
            gx_tuner_strobe(cr, x0, y0, mini_cents);
        }
    } else {
        cents = 100;
        snprintf(s, sizeof(s), "+ - cent");
    }    
    cairo_set_source_rgb (cr, 0.5, 0.5, 0.1);
    cairo_set_font_size (cr, 6.0);
    cairo_text_extents(cr, s, &ex);
    cairo_move_to (cr, x0+28-ex.width, y0+58);
    cairo_show_text(cr, s);

    double ux=2., uy=2.;
    cairo_device_to_user_distance (cr, &ux, &uy);
    if (ux < uy)
        ux = uy;
    cairo_set_line_width (cr, ux + grow);

    // indicator (line)
    cairo_move_to(cr,x0+50, y0+rect_height+5);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_dash (cr, dash_ind, sizeof(dash_ind)/sizeof(dash_ind[0]), 1);
    cairo_line_to(cr, (log_scale(cents, scale)*2*rect_width)+x0+50, y0+(scale*scale*30)+2);
    cairo_set_source_rgb(cr,  0.5, 0.1, 0.1);
    cairo_stroke(cr);   

    g_free (allocation); 
    return FALSE;
}

/*
** paint tuner background picture (the non-changing parts)
*/
static void draw_background(cairo_surface_t *surface) {
    cairo_t *cr;

    double x0      = 0;
    double y0      = 0;

    cr = cairo_create(surface);
    cairo_scale(cr, 3, 3);
    // background
    cairo_rectangle (cr, x0-1,y0-1,rect_width+2,rect_height+2);
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_fill_preserve(cr);
    // light
    cairo_pattern_t*pat =
        cairo_pattern_create_radial (-50, y0, 5,rect_width-10,  rect_height, 20.0);
    cairo_pattern_add_color_stop_rgba (pat, 0, 1, 1, 1, 0.8);
    cairo_pattern_add_color_stop_rgba (pat, 0.3, 0.4, 0.4, 0.4, 0.8);
    cairo_pattern_add_color_stop_rgba (pat, 0.6, 0.05, 0.05, 0.05, 0.8);
    cairo_pattern_add_color_stop_rgba (pat, 1, 0.0, 0.0, 0.0, 0.8);
    cairo_set_source (cr, pat);
    //cairo_rectangle (cr, x0+2,y0+2,rect_width-3,rect_height-3);
    cairo_fill(cr);
     // division scale
    pat = cairo_pattern_create_linear (x0+50, y0,x0, y0);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REFLECT);
    cairo_pattern_add_color_stop_rgb (pat, 0, 0.1, 0.8, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.1, 0.1, 0.6, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.2, 0.3, 0.6, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.3, 0.4, 0.4, 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 1, 0.8, 0.1, 0.1);
    cairo_set_source (cr, pat);
    cairo_set_dash (cr, dashes, sizeof (dashes)/sizeof(dashes[0]), 100.0);
    cairo_set_line_width(cr, 3.0);
    for (int i = -5; i < -1; i++) {
        cairo_move_to(cr,x0+50, y0+rect_height-5);
        cairo_line_to(cr, (((i*0.08))*rect_width)+x0+50, y0+(((i*0.1*i*0.1))*30)+2);
    }
    for (int i = 2; i < 6; i++) {
        cairo_move_to(cr,x0+50, y0+rect_height-5);
        cairo_line_to(cr, (((i*0.08))*rect_width)+x0+50, y0+(((i*0.1*i*0.1))*30)+2);
    }
    cairo_move_to(cr,x0+50, y0+rect_height-5);
    cairo_line_to(cr, x0+50, y0+2);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 1);
    cairo_set_dash (cr, dashes, sizeof (dashes)/sizeof(dashes[0]), 100.0);
    cairo_move_to(cr,x0+50, y0+rect_height-5);
    cairo_line_to(cr, (((-3*0.04))*rect_width)+x0+50, y0+(((-3*0.1*-3*0.1))*30)+2);
    cairo_move_to(cr,x0+50, y0+rect_height-5);
    cairo_line_to(cr, (((-2*0.048))*rect_width)+x0+50, y0+(((-2*0.1*-2*0.1))*30)+2);
    cairo_move_to(cr,x0+50, y0+rect_height-5);
    cairo_line_to(cr, (((3*0.04))*rect_width)+x0+50, y0+(((3*0.1*3*0.1))*30)+2);
    cairo_move_to(cr,x0+50, y0+rect_height-5);
    cairo_line_to(cr, (((2*0.048))*rect_width)+x0+50, y0+(((2*0.1*2*0.1))*30)+2);

    for (int i = -2; i < 3; i++) {
        cairo_move_to(cr,x0+50, y0+rect_height-5);
        cairo_line_to(cr, (((i*0.035))*rect_width)+x0+50, y0+(((i*0.1*i*0.1))*30)+2);
    }
    cairo_stroke(cr);


    pat =
	cairo_pattern_create_linear (x0+30, y0, x0+70, y0);
    
    cairo_pattern_add_color_stop_rgb (pat, 1, 0.2, 0.2 , 0.2);
    cairo_pattern_add_color_stop_rgb (pat, 0.5, 0.1, 0.1 , 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0,0.05, 0.05 , 0.05);
    cairo_set_source (cr, pat);
    cairo_arc(cr, x0+50, y0+rect_height+5, 12.0, 0, 2*M_PI);
    cairo_fill_preserve(cr);
    cairo_set_dash (cr, no_dash, sizeof(no_dash)/sizeof(no_dash[0]), 0);
    cairo_pattern_add_color_stop_rgb (pat, 0, 0.1, 0.1 , 0.1);
    cairo_pattern_add_color_stop_rgb (pat, 0.8, 0.05, 0.05 , 0.05);
    cairo_pattern_add_color_stop_rgb (pat, 1,0.01, 0.01 , 0.01);
    cairo_set_source (cr, pat);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    
    cairo_set_source_rgb(cr,0.1,0.1,0.1);
    gx_tuner_triangle(cr, x0+20, y0+40, 15, 10);
    gx_tuner_triangle(cr, x0+80, y0+40, -15, 10);
    cairo_stroke(cr);

    
    // indicator shaft (circle)
    /*cairo_set_dash (cr, dash_ind, sizeof(dash_ind)/sizeof(dash_ind[0]), 0);
    cairo_move_to(cr, x0+50, y0+rect_height-5);
    cairo_arc(cr, x0+50, y0+rect_height-5, 2.0, 0, 2*M_PI);
    cairo_set_source_rgb(cr,  0.5, 0.1, 0.1);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);*/
    cairo_pattern_destroy(pat);
    cairo_destroy(cr);
}


