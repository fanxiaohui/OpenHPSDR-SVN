#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <fftw3.h>

#include "bandstack.h"
#include "main.h"
#include "filter.h"
#include "property.h"
#include "soundcard.h"
#include "bandscope.h"
#include "vfo.h"

GtkWidget* bandscopeScrolledWindow;
GtkAdjustment* horizontalAdjustment;
GtkAdjustment* verticalAdjustment;
GtkWidget* bandscope;

int bandscopeMAX=80;
int bandscopeMIN=-80;
int bandscopeSTEP=10;

int bandscopeLow=0;
int bandscopeHigh=61440000;

int bandscopeZoom=4;
int bandscopeOffset=0;

float* blackmanHarris;

fftwf_complex* timebuf;
fftwf_complex* freqbuf;
fftwf_plan plan;
float* result;


GdkPixmap *bandscopePixmap=NULL;
GdkPoint* bandscopePoints;

gboolean bandscope_configure_event(GtkWidget* widget,GdkEventConfigure* event);
gboolean bandscope_expose_event(GtkWidget* widget,GdkEventExpose* event);
gboolean bandscope_button_press_event(GtkWidget* widget,GdkEventButton* event);

void plotBandscope(float* samples);
void drawBandscope();
void bandscopeUpdateOff();
float *blackmanHarrisFilter(int n);

GtkWidget* buildBandscopeUI() {
    
    // prepare the fft (time domain to frequency domain)
    timebuf=(fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*BANDSCOPE_BUFFER_SIZE);
    freqbuf=(fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex)*BANDSCOPE_BUFFER_SIZE);
    plan=fftwf_plan_dft_1d(BANDSCOPE_BUFFER_SIZE,timebuf,freqbuf,FFTW_FORWARD,FFTW_ESTIMATE);

    result=malloc(BANDSCOPE_BUFFER_SIZE*sizeof(float));

    // allocate space for the plotting bandscopePoints (allow for 4x zoom)
    bandscopePoints=calloc(bandscopeWIDTH*4,sizeof(GdkPoint));
    int i;
    for(i=0;i<bandscopeWIDTH*4;i++) {
        bandscopePoints[i].x=i;
        bandscopePoints[i].y=-1;
    }

    // build the BlackmanHarris filter
    blackmanHarris=blackmanHarrisFilter(BANDSCOPE_BUFFER_SIZE);

    // build the UI

    horizontalAdjustment=(GtkAdjustment*)gtk_adjustment_new(0.0,0.0,0.0,0.0,0.0,0.0);
    verticalAdjustment=(GtkAdjustment*)gtk_adjustment_new(0.0,0.0,0.0,0.0,0.0,0.0);

    bandscopeScrolledWindow=gtk_scrolled_window_new(horizontalAdjustment,verticalAdjustment);
    gtk_widget_set_size_request(GTK_WIDGET(bandscopeScrolledWindow),bandscopeWIDTH+2,bandscopeHEIGHT);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (bandscopeScrolledWindow),
                                    GTK_POLICY_ALWAYS,GTK_POLICY_NEVER);

    gtk_widget_show(bandscopeScrolledWindow);

    bandscope=gtk_drawing_area_new();
    gtk_widget_set_size_request(GTK_WIDGET(bandscope),bandscopeWIDTH*bandscopeZoom,bandscopeHEIGHT);
    gtk_widget_show(bandscope);

    g_signal_connect(G_OBJECT (bandscope),"configure_event",G_CALLBACK(bandscope_configure_event),NULL);
    g_signal_connect(G_OBJECT (bandscope),"expose_event",G_CALLBACK(bandscope_expose_event),NULL);
    g_signal_connect(G_OBJECT(bandscope),"button_press_event",G_CALLBACK(bandscope_button_press_event),NULL);

    gtk_widget_set_events(bandscope, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);

    gtk_scrolled_window_add_with_viewport((GtkScrolledWindow*)bandscopeScrolledWindow,bandscope);


    return bandscopeScrolledWindow;
}

gboolean bandscope_configure_event(GtkWidget* widget,GdkEventConfigure* event) {
    if(bandscopePixmap) g_object_unref(bandscopePixmap);

    bandscopePixmap=gdk_pixmap_new(widget->window,widget->allocation.width,widget->allocation.height,-1);

    gdk_draw_rectangle(bandscopePixmap,
		       widget->style->black_gc,
		       TRUE,
		       0,0,
		       widget->allocation.width,
		       widget->allocation.height);
    return TRUE;
}

gboolean bandscope_expose_event(GtkWidget* widget,GdkEventExpose* event) {
    gdk_draw_drawable(widget->window,
		    widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
		    bandscopePixmap,
		    event->area.x, event->area.y,
		    event->area.x, event->area.y,
		    event->area.width, event->area.height);
    return FALSE;
}

gboolean bandscope_button_press_event(GtkWidget* widget,GdkEventButton* event) {
    float hzPerPixel;
    long long f;
    switch(event->button) {
        case 1:
            // left button - click to frequency (to cursor) 
            hzPerPixel=(float)bandscopeHigh/(float)bandscopeZoom/(float)bandscopeWIDTH;
            f=(long long)((float)event->x*hzPerPixel);
            setAFrequency(f);
            break;
        case 2:
            // middle button
            break;
        case 3:
            // right button
            break;
    }
    return TRUE;
}

void updateBandscope(float* samples) {
    int i,j;
    int half=BANDSCOPE_BUFFER_SIZE/2;
    // copy samples into time domain array
    for(i=0;i<BANDSCOPE_BUFFER_SIZE;i++) {
        timebuf[i][0]=samples[i]*blackmanHarris[i];
        timebuf[i][1]=0.0f;
    }

    // perform the fft
    fftwf_execute(plan);

    // compute the power levels
    for(i=0;i<BANDSCOPE_BUFFER_SIZE;i++) {
        result[i]=10.0f*log(sqrt(freqbuf[i][0]*freqbuf[i][0] +
                  freqbuf[i][1]*freqbuf[i][1]));
    }
    
    plotBandscope(result);
    drawBandscope();
}

void drawBandscope() {

    // get the bandscope context - just copy the window GC and modify
    GdkGC* gc;
    PangoContext *context;
    PangoLayout *layout;
    char label[80];

    int i,x,x1;
    long long f;
    float hzPerPixel;

    f=bandscopeLow;
    hzPerPixel=(float)bandscopeHigh/(float)(bandscopeWIDTH*bandscopeZoom);

    if(bandscope->window) {

        gc=gdk_gc_new(bandscope->window);
        gdk_gc_copy(gc,bandscope->style->black_gc);


        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,0,0,bandscopeWIDTH*bandscopeZoom,bandscopeHEIGHT);

        // setup for drawing text
        context = gdk_pango_context_get_for_screen (gdk_screen_get_default ());
        layout = pango_layout_new (context);
        pango_layout_set_width(layout,bandscopeWIDTH*PANGO_SCALE);

        // show the ham bands
        gdk_gc_set_rgb_fg_color(gc,&grey);

        x=(int)(1800000.0f/hzPerPixel);
        x1=(int)(2000000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(3500000.0f/hzPerPixel);
        x1=(int)(4000000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(5330500.0f/hzPerPixel);
        x1=(int)(5403500.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(7000000.0f/hzPerPixel);
        x1=(int)(7300000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(10100000.0f/hzPerPixel);
        x1=(int)(10150000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(14000000.0f/hzPerPixel);
        x1=(int)(14350000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(18068000.0f/hzPerPixel);
        x1=(int)(18168000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(21000000.0f/hzPerPixel);
        x1=(int)(21450000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(24890000.0f/hzPerPixel);
        x1=(int)(24990000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(28000000.0f/hzPerPixel);
        x1=(int)(29700000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        x=(int)(50000000.0f/hzPerPixel);
        x1=(int)(54000000.0f/hzPerPixel);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,x,0,x1-x,bandscopeHEIGHT);
        

        // draw the frequency markers
        for(i=5;i<65;i+=5) {
            x=(int)(((float)i*1000000.0f)/hzPerPixel);
            gdk_gc_set_rgb_fg_color(gc,&verticalColor);
            gdk_draw_line(bandscopePixmap,gc,x,0,x,bandscopeHEIGHT);
            gdk_gc_set_rgb_fg_color(gc,&spectrumTextColor);
            sprintf(label,"<span font_desc='Sans Regular 8'>%4.2f</span>",(float)i);
            pango_layout_set_markup (layout, label, -1);
            gdk_draw_layout (GDK_DRAWABLE (bandscopePixmap), gc, x-17, 0, layout);
        }
        
        // draw the cursor
        gdk_gc_set_rgb_fg_color(gc,&red);
        x=(int)((float)frequencyA/hzPerPixel);
        gdk_draw_line(bandscopePixmap,gc,x,0,x,bandscopeHEIGHT);

/*
        // draw the horizontal lines
        int v=bandscopeMAX-bandscopeMIN;
        int n=v/bandscopeSTEP;
        int p=bandscopeHEIGHT/n;
        int i;

        for(i=1;i<n;i++) {
            int val=bandscopeMAX-i*bandscopeSTEP;
            int y=(bandscopeMAX-val)*bandscopeHEIGHT/v;
            gdk_gc_set_rgb_fg_color(gc,&horizontalColor);
            gdk_draw_line(bandscopePixmap,gc,0,y,bandscopeWIDTH,y);
            gdk_gc_set_rgb_fg_color(gc,&spectrumTextColor);
            sprintf(label,"<span font_desc='Sans Regular 8'>%d</span>",val);
            pango_layout_set_markup (layout, label, -1);
            gdk_draw_layout (GDK_DRAWABLE (bandscopePixmap), gc, 0, y, layout);
        }
*/

        // draw the bandscope
        gdk_gc_set_rgb_fg_color(gc,&plotColor);
        gdk_draw_lines(bandscopePixmap,gc,bandscopePoints,bandscopeWIDTH*bandscopeZoom);

        // update the bandscope
        gtk_widget_queue_draw(bandscope);

        g_object_unref(context);
        g_object_unref(layout);
        g_object_unref(gc);
    }
}

void plotBandscope(float* samples) {
        float samplesPerPixel=(float)BANDSCOPE_BUFFER_SIZE/2.0/(float)(bandscopeWIDTH*bandscopeZoom);
        float max=0.0f;
        float yRange = (float)(bandscopeMAX - bandscopeMIN);

        int i,j;

        if(samplesPerPixel<1.0f) samplesPerPixel=1.0f;
        for(i=0;i<bandscopeWIDTH*bandscopeZoom;i++) {
            int offset=(int)(samplesPerPixel*(float)i);
            max=-300.0f;
            for(j=offset;j<offset+samplesPerPixel;j++) {
                  if(samples[j]>max) max=samples[j];
            }
            bandscopePoints[i].x=i;
            bandscopePoints[i].y=(int)(floor((bandscopeMAX - max)*(float)bandscopeHEIGHT/yRange));
        }

}

void bandscopeUpdateOff() {

    // get the bandscope context - just copy the window GC and modify
    GdkGC* gc;
    if(bandscope->window) {
        gc=gdk_gc_new(bandscope->window);
        gdk_gc_copy(gc,bandscope->style->black_gc);
        gdk_draw_rectangle(bandscopePixmap,gc,TRUE,0,0,bandscopeWIDTH,bandscopeHEIGHT);

        // update the bandscope
        gtk_widget_queue_draw_area(bandscope,0,0,bandscopeWIDTH,bandscopeHEIGHT);
    }
}

void bandscopeSaveState() {
    char string[128];

    sprintf(string,"%d",bandscopeMAX);
    setProperty("bandscope.max",string);

    sprintf(string,"%d",bandscopeMIN);
    setProperty("bandscope.min",string);

    sprintf(string,"%d",bandscopeSTEP);
    setProperty("bandscope.step",string);

}

void bandscopeRestoreState() {
    char* value;

    value=getProperty("bandscope.max");
    if(value) bandscopeMAX=atoi(value);

    value=getProperty("bandscope.min");
    if(value) bandscopeMIN=atoi(value);

    value=getProperty("bandscope.step");
    if(value) bandscopeSTEP=atoi(value);

}

void bandscopeSetZoom(int zoom) {
    bandscopeZoom=zoom;
    gtk_widget_set_size_request(GTK_WIDGET(bandscope),bandscopeWIDTH*bandscopeZoom,bandscopeHEIGHT);
}

float *blackmanHarrisFilter(int n) {
    float* filter;
    float a0=0.35875F,
          a1=0.48829F,
          a2=0.14128F,
          a3=0.01168F;
    float twopi=M_PI*2.0F;
    float fourpi=M_PI*4.0F;
    float sixpi=M_PI*6.0F;
    int i;

    filter=malloc(n*sizeof(float));

    for(i = 0;i<n;i++) {
        filter[i]=a0
                  - a1 * cos(twopi  * (i + 0.5) / n)
                  + a2 * cos(fourpi * (i + 0.5) / n)
                  - a3 * cos(sixpi * (i + 0.5) / n);
    }

    return filter;

}
