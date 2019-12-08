/*
 * File:        ADC > FFT > TFT
 * Author:      Bruce Land
 * 
 * Target PIC:  PIC32MX250F128B
 * 
 * ECE 4760 Final Project
 * PICcompose
 * Diane Sutyak, Laasya Renganathan, Tara van Nieuwstadt
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config_1_3_2.h"
// threading library
#include "pt_cornell_1_3_2.h"
// port expander
#include "port_expander_brl4.h"

////////////////////////////////////
// graphics libraries
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
#include <math.h>
////////////////////////////////////


/* Demo code for interfacing TFT (ILI9340 controller) to PIC32
 * The library has been modified from a similar Adafruit library
 */
// Adafruit data:
/***************************************************
  This is an example sketch for the Adafruit 2.2" SPI display.
  This library works with the Adafruit 2.2" TFT Breakout w/SD card
  ----> http://www.adafruit.com/products/1480

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/


#define start_spi2_critical_section INTEnable(INT_T2, 0);
#define end_spi2_critical_section INTEnable(INT_T2, 1);

// === the fixed point macros ========================================
typedef signed short fix14 ;
#define multfix14(a,b) ((fix14)((((long)(a))*((long)(b)))>>14)) //multiply two fixed 2.14
#define float2fix14(a) ((fix14)((a)*16384.0)) // 2^14
#define fix2float14(a) ((float)(a)/16384.0)
#define absfix14(a) abs(a) 
// === input arrays ==================================================
#define nSamp 512
#define nPixels 256
fix14 v_in[nSamp] ;
#define ZERO 48 //try to relate to BIN_IGNORE?

// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_fft ;
static struct pt pt_met;
static struct pt pt_timer;
static struct pt pt_key;

// === MIDI bytes ====================================================
/*Status byte is 128 for NOTE OFF, 144 for NOTE ON*/                 
int midi_sb = 128; //NOTE_ON/NOTE_OFF, MIDI channel
int note_length; //not something we have to send over
int midi_db1; //pitch value
int midi_db1_prev; //pitch from prev second
int midi_db1_prev_fft; //pitch from previous fft run
int midi_db2; //velocity value

int bpm = 60; //metronome tempo
int bpm_array[5];
int bpm_index;
int record_mode; //record mode status
int prev_record_mode;
int record_time; //running time of recording in milliseconds
int tempo_color = ILI9340_GREEN;
int octave;
int tempo_lock;
int keypad, i;

// bit pattern for each row of the keypad scan -- active LOW
// bit zero low is first entry
char out_table[4] = {0b1110, 0b1101, 0b1011, 0b0111};

// the read-pattern if no button is pulled down by an output
#define no_button (0x70)

// order is 0 thru 9 then * ==10 and # ==11
// no press = -1
// table is decoded to natural digit order (except for * and #)
// with shift key codes for each key
// keys 0-9 return the digit number
// keys 10 and 11 are * adn # respectively
// Keys 12 to 21 are the shifted digits
// keys 22 and 23 are shifted * and # respectively
static int keytable[24]=
//        0     1      2    3     4     5     6      7    8     9    10-*  11-#
        {0xd7, 0xbe, 0xde, 0xee, 0xbd, 0xdd, 0xed, 0xbb, 0xdb, 0xeb, 0xb7, 0xe7,
//        s0     s1    s2  s3    s4    s5    s6     s7   s8    s9    s10-* s11-#
         0x57, 0x3e, 0x5e, 0x6e, 0x3d, 0x5d, 0x6d, 0x3b, 0x5b, 0x6b, 0x37, 0x67};


//==============FSM===========================
volatile int prev_i = -1;
volatile int PushState = 0;
volatile int press;
#define NoPush 0
#define MaybePush 1
#define  Pushed 2
#define MaybeNoPush 3  
//=============================================


// == Smoothing Signal ==============================================
#define ROUND(x) ((x)>=0?(int)(x+0.5):(int)(x-0.5))
#define num_samples 10 //this would be used if we kept samples for averaging/mode purposes
int samples[num_samples];


//this should return the average rounded to the nearest int. Haven't tested this yet
int average(int num, int array[]){
        int i;
        int sum;
        for (i= 0; i<num_samples; i++){
            sum += array[i];
        }
        return ROUND(sum/num_samples);
}


// === print line for TFT ============================================
// print a line on the TFT
// string buffer
char buffer[60];
void printLine1(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 10 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 239, 8, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(1);
    tft_writeString(print_buffer);
}

void printLine2(int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 20 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 300, 16, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(2);
    tft_writeString(print_buffer);
}

void printLine(int size, int line_number, char* print_buffer, short text_color, short back_color){
    // line number 0 to 31 
    /// !!! assumes tft_setRotation(0);
    // print_buffer is the string to print
    int v_pos;
    v_pos = line_number * 20 ;
    // erase the pixels
    tft_fillRoundRect(0, v_pos, 300, 8*size, 1, back_color);// x,y,w,h,radius,color
    tft_setTextColor(text_color); 
    tft_setCursor(0, v_pos);
    tft_setTextSize(size);
    tft_writeString(print_buffer);
}

//=== FFT ==============================================================
// FFT
#define N_WAVE          1024    /* size of FFT 512 */
#define LOG2_N_WAVE     10     /* log2(N_WAVE) 0 */
#define begin {
#define end }
#define BIN_WIDTH       8
#define BIN_IGNORE      15     /* number of garbage bins to ignore */

fix14 Sinewave[N_WAVE]; // a table of sines for the FFT
fix14 window[N_WAVE]; // a table of window values for the FFT
fix14 fr[N_WAVE], fi[N_WAVE];
int pixels[nPixels] ;

char tones[12][3] = { "C", "C#", "D", "Eb", "E", "F",
         "F#", "G", "Ab", "A", "Bb", "B" };

void FFTfix(fix14 fr[], fix14 fi[], int m)
//Adapted from code by:
//Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com
//fr[n],fi[n] are real,imaginary arrays, INPUT AND RESULT.
//size of data = 2**m
// This routine does foward transform only
begin
    int mr,nn,i,j,L,k,istep, n;
    int qr,qi,tr,ti,wr,wi;

    mr = 0;
    n = 1<<m;   //number of points
    nn = n - 1;

    /* decimation in time - re-order data */
    for(m=1; m<=nn; ++m)
    begin
        L = n;
        do L >>= 1; while(mr+L > nn);
        mr = (mr & (L-1)) + L;
        if(mr <= m) continue;
        tr = fr[m];
        fr[m] = fr[mr];
        fr[mr] = tr;
        //ti = fi[m];   //for real inputs, don't need this
        //fi[m] = fi[mr];
        //fi[mr] = ti;
    end

    L = 1;
    k = LOG2_N_WAVE-1;
    while(L < n)
    begin
        istep = L << 1;
        for(m=0; m<L; ++m)
        begin
            j = m << k;
            wr =  Sinewave[j+N_WAVE/4];
            wi = -Sinewave[j];
            //wr >>= 1; do need if scale table
            //wi >>= 1;

            for(i=m; i<n; i+=istep)
            begin
                j = i + L;
                tr = multfix14(wr,fr[j]) - multfix14(wi,fi[j]);
                ti = multfix14(wr,fi[j]) + multfix14(wi,fr[j]);
                qr = fr[i] >> 1;
                qi = fi[i] >> 1;
                fr[j] = qr - tr;
                fi[j] = qi - ti;
                fr[i] = qr + tr;
                fi[i] = qi + ti;
            end
        end
        --k;
        L = istep;
    end
end

// === FFT Thread =============================================
// DMA channel busy flag
#define CHN_BUSY 0x80
#define log_min 0x10   
static PT_THREAD (protothread_fft(struct pt *pt))
{
    PT_BEGIN(pt);
    static int sample_number ;
    static fix14 zero_point_4 = float2fix14(0.4) ;
    // approx log calc ;
    static int sx, y, ly, temp ;
    
    while(1) {
        // yield time 30 milliseconds
        PT_YIELD_TIME_msec(30);
        
        // enable ADC DMA channel and get
        // 512 samples from ADC
        DmaChnEnable(0);
        // yield until DMA done: while((DCH0CON & Chn_busy) ){};
        PT_WAIT_WHILE(pt, DCH0CON & CHN_BUSY);
        // 
        // profile fft time
        WriteTimer4(0);
        
        // compute and display fft
        // load input array
        for (sample_number=0; sample_number<nSamp-1; sample_number++){
            if (sample_number<256) {v_in[sample_number] = v_in[sample_number]*(1/256)*sample_number;}
            else {
                v_in[sample_number] = v_in[sample_number]*(1-(1/256)*(sample_number-256));
            }
            
            fr[sample_number] = multfix14(v_in[sample_number], window[sample_number]); 
            fi[sample_number] = 0 ;
        }
        
        // do FFT
        FFTfix(fr, fi, LOG2_N_WAVE);
        // get magnitude and log
        // The magnitude of the FFT is approximated as: 
        //   |amplitude|=max(|Re|,|Im|)+0.4*min(|Re|,|Im|). 
        // This approximation is accurate within about 4% rms.
        // https://en.wikipedia.org/wiki/Alpha_max_plus_beta_min_algorithm
        for (sample_number = 0; sample_number < nPixels; sample_number++) {  
            // get the approx magnitude
            fr[sample_number] = abs(fr[sample_number]); //>>9
            fi[sample_number] = abs(fi[sample_number]);
            // reuse fr to hold magnitude
            fr[sample_number] = max(fr[sample_number], fi[sample_number]) + 
                    multfix14(min(fr[sample_number], fi[sample_number]), zero_point_4); 
            
            // reuse fr to hold log magnitude
            // shifting finds most significant bit
            // then make approxlog  = ly + (fr-y)./(y) + 0.043;
            // BUT for an 8-bit approx (4 bit ly and 4-bit fraction)
            // ly 1<=ly<=14
            // omit the 0.043 because it is too small for 4-bit fraction
            
            sx = fr[sample_number];
            y=1; ly=0;
            while(sx>1) {
               y=y*2; ly=ly+1; sx=sx>>1;
            }
            // shift ly into upper 4-bits as integer part of log
            // take bits below y and shift into lower 4-bits
            fr[sample_number] = ((ly)<<4) + ((fr[sample_number]-y)>>(ly-4) ) ;
            // bound the noise at low amp
            if(fr[sample_number]<log_min) fr[sample_number] = log_min;
        }
        
        
        // Display on TFT
        int max_amplitude = 0;
        float max_frequency = 1;
        midi_db1 = 1; //data byte 1
        // erase, then draw
        for (sample_number = 0; sample_number < nPixels; sample_number++) {
            //tft_drawPixel(sample_number, pixels[sample_number], ILI9340_BLACK);
            pixels[sample_number] = -fr[sample_number] + 200 ;
            //tft_drawPixel(sample_number, pixels[sample_number], ILI9340_WHITE);
            if ((fr[sample_number] > max_amplitude) && (sample_number > BIN_IGNORE)) {
                max_amplitude = fr[sample_number];
                max_frequency = sample_number * BIN_WIDTH; // bin width
            }
            // reuse fr to hold magnitude 
        }
        
        
        /* Three cases:
         Case 1: start note, no previous note
         Case 2: End note, to silence
         Case 3: End note, immediately start new note
        if (midi_db1==48){
            midi_sb = 128;
            if (midi_db1_prev != 48) {//Case 2
            //send message now
            }
        } 
        else if (midi_db1 != midi_db1_prev) {
            if (midi_db1_prev!=48){ //Case 3
            midi_sb = 128;
            //send message now
            }
            midi_sb = 144; //Case 1, 3
            //send message now
        }*/
        
        //sprintf(buffer, "PEAK FREQ: %.0f Hz", max_frequency );
        //printLine2(1, buffer, ILI9340_WHITE, ILI9340_BLACK);
        
        midi_db1 = (int) 12*(log(max_frequency/440)/log(2)) + 69;
        
        //sprintf(buffer, "MIDI DB1 Value: %d", midi_db1 );
        //printLine2(7, buffer, ILI9340_WHITE, ILI9340_BLACK);
        
        octave = (int) max((midi_db1 / 12) - 1, 0);
        
        /*if (midi_db1 != midi_db1_prev_fft) {
          cursor_pos(4,1);
          sprintf(PT_send_buffer,"MIDI %d",midi_db1);
          // by spawning a print thread
          PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
          clr_right;
          normal_text;
        }
        midi_db1_prev_fft = midi_db1;*/
        
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // FFT thread


// === Metronome Thread =============================================
    
static PT_THREAD (protothread_met(struct pt *pt))
{
    PT_BEGIN(pt);
    mPORTBSetBits(BIT_3);   //Clear bits to ensure light is off.
    mPORTBSetPinsDigitalOut(BIT_3 );    //Set port as output
    mPORTBClearBits(BIT_3);
    while(1) {
        // yield time 1 second, 60bpm
        PT_YIELD_TIME_msec(1000*30/bpm);
        
        if (record_mode) {
            mPORTBToggleBits(BIT_3); //toggle LED
        }
        else {mPORTBClearBits(BIT_3);}
        
        //if (midi_db1_prev != midi_db1){note_length = 0;}
        //else if (midi_db1 != 48){note_length++;}
        
        //sprintf(buffer, "Note length: %d", note_length);
        //printLine2(4, buffer, ILI9340_WHITE, ILI9340_BLACK);
        
        //midi_db1_prev = midi_db1;
      
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // Metronome thread


// === Timer Thread =============================================

static PT_THREAD (protothread_timer(struct pt *pt))
{
    PT_BEGIN(pt);
    
    mPortYSetPinsIn(BIT_0);
   
    while(1) {
        // yield time 1 millisecond
        PT_YIELD_TIME_msec(1);
        
        start_spi2_critical_section;
        prev_record_mode = record_mode;
        record_mode = (readPE(GPIOZ) & 0x1);
        end_spi2_critical_section;
        
        
        if (record_mode) {
            
            if (!prev_record_mode){
                if (!tempo_lock){
                    bpm = 120;
                    sprintf(PT_send_buffer,"~ BPM %d ~\n",bpm);
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                }
                sprintf(PT_send_buffer,"~ BEGIN ~\n");
                PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                tft_fillScreen(ILI9340_BLACK);
                sprintf(buffer, "Tempo: %d BPM", bpm); 
                printLine(2,1, buffer, ILI9340_WHITE, ILI9340_BLACK);
                sprintf(buffer,"Note Name:");
                printLine(5,3, buffer, ILI9340_YELLOW, ILI9340_BLACK);
                sprintf(buffer,"   -");
                printLine(5, 5, buffer, ILI9340_YELLOW, ILI9340_BLACK);
                sprintf(buffer, "RECORDING");
                printLine(2, 10, buffer, ILI9340_MAGENTA, ILI9340_BLACK);
                sprintf(buffer, "FLIP SWITCH TO STOP RECORD");
                printLine(2, 11, buffer, ILI9340_WHITE, ILI9340_BLACK);
            }
            
            if (midi_db1_prev != midi_db1) {
              // Send MIDI note encoding
              //cursor_pos(4,1);
                
                //sprintf(buffer, "MIDI DB1 Value: %d", midi_db1 );
                //printLine2(7, buffer, ILI9340_WHITE, ILI9340_BLACK);
                //sprintf(buffer, "MIDI DB1 Value: %d", midi_db1_prev );
                //printLine2(9, buffer, ILI9340_WHITE, ILI9340_BLACK);
                
                if (midi_db1_prev != ZERO) {
                    // Send NOTE STOP message
                    sprintf(PT_send_buffer,"~ STOP %d %d ~\n",record_time,midi_db1_prev);
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                }
                if (midi_db1 != ZERO) {
                    // Send NOTE START message
                    sprintf(PT_send_buffer,"~ START %d %d ~\n",record_time,midi_db1);
                    PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                    // make this big
                    sprintf(buffer,"   %s%d", tones[midi_db1%12], octave );
                    printLine(5, 5, buffer, ILI9340_YELLOW, ILI9340_BLACK);
                } else {
                    sprintf(buffer,"   -");
                    printLine(5, 5, buffer, ILI9340_YELLOW, ILI9340_BLACK);
                }
              
              //clr_right;
              //normal_text;
              // reset note length
              note_length = 0;
            }
            //else if (midi_db1 != ZERO) note_length++;

            //sprintf(buffer, "Note length: %d", note_length);
            //printLine2(4, buffer, ILI9340_WHITE, ILI9340_BLACK);

            midi_db1_prev = midi_db1;
            record_time++;
          
        } else {
            
            if (prev_record_mode){
                sprintf(PT_send_buffer,"~ END ~\n");
                PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                tempo_color = ILI9340_GREEN;
                tft_fillScreen(ILI9340_BLACK);
                sprintf(buffer, "Note Name: -"); 
                printLine(2,1, buffer, ILI9340_WHITE, ILI9340_BLACK);
                sprintf(buffer,"Tempo:");
                printLine(5,3, buffer, tempo_color, ILI9340_BLACK);
                sprintf(buffer,"   %d BPM", bpm);
                printLine(5, 5, buffer, tempo_color, ILI9340_BLACK);
                sprintf(buffer, "PRESS # TO ENTER TEMPO");
                printLine(2, 10, buffer, ILI9340_MAGENTA, ILI9340_BLACK);
                sprintf(buffer, "PRESS * TO CLEAR");
                printLine(2, 11, buffer, ILI9340_WHITE, ILI9340_BLACK);
            }
            
            if (midi_db1_prev != midi_db1) {
                if (midi_db1 != ZERO){
                    tft_fillRoundRect(130, 14, 40, 30, 1, ILI9340_BLACK);// x,y,w,h,radius,color
                    tft_setCursor(130,20);
                    tft_setTextColor(ILI9340_WHITE); tft_setTextSize(2);
                    sprintf(buffer, "%s%d", tones[midi_db1%12], octave); 
                    tft_writeString(buffer);
                } else {
                    tft_fillRoundRect(130, 14, 40, 30, 1, ILI9340_BLACK);// x,y,w,h,radius,color
                    tft_setCursor(130,20);
                    tft_setTextColor(ILI9340_WHITE); tft_setTextSize(2);
                    sprintf(buffer, "-"); 
                    tft_writeString(buffer);
                }
            }
            midi_db1_prev = midi_db1;
            
        }
        
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // Timer thread


// === Keypad Thread =============================================

static PT_THREAD (protothread_key(struct pt *pt))
{
    PT_BEGIN(pt);
    static int pattern, bpm_disp, j;
    //tempo_lock is 0 for unlocked, 1 for locked
    
    // init the port expander
    start_spi2_critical_section;
    
    // PortY on Expander ports as digital outputs
    mPortYSetPinsOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortY as inputs
    // note that bit 7 will be shift key input, 
    // separate from keypad
    mPortYSetPinsIn(BIT_4 | BIT_5 | BIT_6 | BIT_7);    //Set port as input
    mPortYEnablePullUp(BIT_4 | BIT_5 | BIT_6 | BIT_7);
    
    
    end_spi2_critical_section ;

      while(1) {
        // yield time
        PT_YIELD_TIME_msec(30);
        
        if (!record_mode) {
            if (prev_record_mode){
                tempo_lock = 0;
                bpm_index=0;
                bpm_disp = 0;             
                sprintf(buffer,"   %d BPM", bpm_disp);
                printLine(5, 5, buffer, tempo_color, ILI9340_BLACK);
                
            }
            for (i=0; i<4; i++) {
                start_spi2_critical_section;
                // scan each rwo active-low
                writePE(GPIOY, out_table[i]);
                //reading the port also reads the outputs
                keypad  = readPE(GPIOY);
                end_spi2_critical_section;
                // was there a keypress?
                if((keypad & no_button) != no_button) { break;}
            }

            // search for keycode
            if (keypad > 0){ // then button is pushed
                for (i=0; i<24; i++){
                    if (keytable[i]==keypad) break;
                }
                // if invalid, two button push, set to -1
                if (i==24) i=-1;
            }
            else i = -1; // no button pushed
            
            //debouncing FSM
            switch (PushState) {
                case NoPush:
                    if (i != -1) {
                        PushState=MaybePush;
                        prev_i = i;
                    }
                    else PushState=NoPush;
                    break;

                case MaybePush:
                    if (i == prev_i) {
                        PushState=Pushed; 
                        //construct 3-digit bpm
                        if (i>-1 && i<10) {
                            if ((bpm_index<3) && !tempo_lock){
                                bpm_array[bpm_index] = i;
                                bpm_index++;
                            }
                        }
                        if (i==10){ //press * to clear or cancel
                            tempo_color = ILI9340_GREEN;
                            for (j = 0; j<5; j++){
                                bpm_array[j]=0;
                            }
                            bpm_index = 0;
                            if (tempo_lock) {
                                sprintf(buffer,"Tempo:");
                                printLine(5,3, buffer, tempo_color, ILI9340_BLACK);
                                sprintf(buffer, "PRESS # TO ENTER TEMPO");
                                printLine(2, 10, buffer, ILI9340_MAGENTA, ILI9340_BLACK);
                                sprintf(buffer, "PRESS * TO CLEAR");
                                printLine(2, 11, buffer, ILI9340_WHITE, ILI9340_BLACK);
                            }
                            tempo_lock = 0; //unlock tempo
                        }
                        if ((i==11) && !tempo_lock){
                            //cursor_pos(4,1);
                            if (bpm_disp == 0){
                                // round rect to clear other text
                                sprintf(buffer, "INVALID TEMPO");
                                printLine(2, 10, buffer, ILI9340_MAGENTA, ILI9340_BLACK);
                            }
                            else {
                                bpm = bpm_disp;
                                sprintf(PT_send_buffer,"~ BPM %d ~\n",bpm);
                                PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
                                tempo_color = ILI9340_RED;
                                sprintf(buffer,"Tempo:");
                                printLine(5,3, buffer, tempo_color, ILI9340_BLACK);
                                sprintf(buffer, "READY TO RECORD");
                                printLine(2, 10, buffer, ILI9340_MAGENTA, ILI9340_BLACK);
                                tempo_lock = 1;
                            }
                            sprintf(buffer, "PRESS * TO RESET TEMPO");
                            printLine(2, 11, buffer, ILI9340_WHITE, ILI9340_BLACK);
                            
                        }
                                                
                        if (!tempo_lock){
                            switch(bpm_index){
                                case 0:
                                    //nothing was entered
                                    bpm_disp = 0;
                                    break;
                                case 1:
                                    bpm_disp = bpm_array[0];
                                    break;
                                case 2:
                                    bpm_disp = (bpm_array[0]*10)+bpm_array[1];
                                    break;
                                case 3:
                                    bpm_disp = (bpm_array[0]*100)+(bpm_array[1]*10)+bpm_array[2];
                                    break;
                                default:
                                    bpm_disp = 0;
                                    break;
                            }
                            
                        }

                    }
                    else PushState=NoPush;
                    break;

                case Pushed:  
                    if (i == prev_i) PushState=Pushed; //FIX: i==prev_i
                    else PushState=MaybeNoPush;    
                    break;

                case MaybeNoPush:
                    if (i == prev_i) PushState=Pushed;
                    else PushState=NoPush;    
                    break;
            } // end case
        }

            // draw key number
            if (i>-1 && i<12) {sprintf(buffer,"   %d BPM", bpm_disp);}
            //if (i==10 ) {sprintf(buffer,"Tempo: *");}
            //if (i==11 ) sprintf(buffer,"Tempo: #");
            //if (i>11 && i<22 ) sprintf(buffer, "Tempo: shift-%d", i-12);
            //if (i==22 ) sprintf(buffer,"Tempo: shift-*");
            //if (i==23 ) sprintf(buffer,"Tempo: shift-#");
            if (i>-1 && i<12) printLine(5, 5, buffer, tempo_color, ILI9340_BLACK);
            else if (i>-1) printLine2(2, buffer, ILI9340_RED, ILI9340_BLACK);
            
            //sprintf(PT_send_buffer,"Key: %d\n",i);
            //PT_SPAWN(pt, &pt_DMA_output, PT_DMA_PutSerialBuffer(&pt_DMA_output) );
        
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // Keypad thread


// === Main  ======================================================

void main(void) {
    //SYSTEMConfigPerformance(PBCLK);

    ANSELA = 0;
    ANSELB = 0;

    // === config threads ==========
    // turns OFF UART support and debugger pin, unless defines are set
    PT_setup();



    // the ADC ///////////////////////////////////////
    
    // timer 3 setup for adc trigger  ==============================================
    // Set up timer3 on,  no interrupts, internal clock, prescalar 1, compare-value
    // This timer generates the time base for each ADC sample. 
    // works at ??? Hz
    #define sample_rate 8000
    // 40 MHz PB clock rate
    #define timer_match 40000000/sample_rate
    OpenTimer3(T3_ON | T3_SOURCE_INT | T3_PS_1_1, timer_match); 
    
    //=== DMA Channel 0 transfer ADC data to array v_in ================================
    // Open DMA Chan1 for later use sending video to TV
    #define DMAchan0 0
    DmaChnOpen(DMAchan0, 0, DMA_OPEN_DEFAULT);
    DmaChnSetTxfer(DMAchan0, (void*)&ADC1BUF0, (void*)v_in, 2, nSamp*2, 2); //512 16-bit integers
    DmaChnSetEventControl(DMAchan0, DMA_EV_START_IRQ(28)); // 28 is ADC done
    // ==============================================================================
    
    // configure and enable the ADC
    CloseADC10(); // ensure the ADC is off before setting the configuration

    // define setup parameters for OpenADC10
    // Turn module on | ouput in integer | trigger mode auto | enable autosample
    // ADC_CLK_AUTO -- Internal counter ends sampling and starts conversion (Auto convert)
    // ADC_AUTO_SAMPLING_ON -- Sampling begins immediately after last conversion completes; SAMP bit is automatically set
    // ADC_AUTO_SAMPLING_OFF -- Sampling begins with AcquireADC10();
    #define PARAM1  ADC_FORMAT_INTG16 | ADC_CLK_TMR | ADC_AUTO_SAMPLING_ON //

    // define setup parameters for OpenADC10
    // ADC ref external  | disable offset test | disable scan mode | do 1 sample | use single buf | alternate mode off
    #define PARAM2  ADC_VREF_AVDD_AVSS | ADC_OFFSET_CAL_DISABLE | ADC_SCAN_OFF | ADC_SAMPLES_PER_INT_1 | ADC_ALT_BUF_OFF | ADC_ALT_INPUT_OFF
    //
    // Define setup parameters for OpenADC10
    // for a 40 MHz PB clock rate
    // use peripherial bus clock | set sample time | set ADC clock divider
    // ADC_CONV_CLK_Tcy should work at 40 MHz.
    // ADC_SAMPLE_TIME_6 seems to work with a source resistance < 1kohm
    #define PARAM3 ADC_CONV_CLK_PB | ADC_SAMPLE_TIME_6 | ADC_CONV_CLK_Tcy //ADC_SAMPLE_TIME_5| ADC_CONV_CLK_Tcy2

    // define setup parameters for OpenADC10
    // set AN11 and  as analog inputs
    #define PARAM4  ENABLE_AN11_ANA // pin 24

    // define setup parameters for OpenADC10
    // do not assign channels to scan
    #define PARAM5  SKIP_SCAN_ALL

    // use ground as neg ref for A | use AN11 for input A     
    // configure to sample AN11 
    SetChanADC10(ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN11); // configure to sample AN4 
    OpenADC10(PARAM1, PARAM2, PARAM3, PARAM4, PARAM5); // configure ADC using the parameters defined above

    EnableADC10(); // Enable the ADC
    ///////////////////////////////////////////////////////

    // timer 4 setup for profiling code ===========================================
    // Set up timer2 on,  interrupts, internal clock, prescalar 1, compare-value
    // This timer generates the time base for each horizontal video line
    OpenTimer4(T4_ON | T4_SOURCE_INT | T4_PS_1_8, 0xffff);
        
    // === init FFT data =====================================
    // one cycle sine table
    //  required for FFT
    int ii;
    for (ii = 0; ii < N_WAVE; ii++) {
        Sinewave[ii] = float2fix14(sin(6.283 * ((float) ii) / N_WAVE)*0.5);
        window[ii] = float2fix14(1.0 * (1.0 - cos(6.283 * ((float) ii) / (N_WAVE - 1))));
        //window[ii] = float2fix(1.0) ;
    }
    // ========================================================
    
    initPE();
    
    // init the Z port to try other output functions
    mPortZSetPinsIn(BIT_0 | BIT_1);    //Set port as input
  
    mPORTBSetPinsDigitalIn(BIT_3);
    EnablePullUpB(BIT_3);
    
    // init the threads
    PT_INIT(&pt_fft);
    PT_INIT(&pt_met);
    PT_INIT(&pt_timer);
    PT_INIT(&pt_key);
    
    // init the display
    tft_init_hw();
    tft_begin();
    tft_fillScreen(ILI9340_BLACK);
    //240x320 vertical display
    tft_setRotation(1); // Use tft_setRotation(1) for 320x240
    
    // Welcome Screen
    sprintf(buffer, "Welcome to \nPICcompose");
    printLine(4, 3, buffer, ILI9340_CYAN, ILI9340_BLACK);
    sprintf(buffer, "Press # to Start");
    printLine(2, 7, buffer, ILI9340_BLUE, ILI9340_BLACK);
    
    tft_fillCircle(255,125,10,ILI9340_WHITE);
    tft_fillCircle(295,150,10,ILI9340_WHITE);
    
    tft_drawLine(265,125,265,55,ILI9340_WHITE);
    tft_drawLine(266,125,266,55,ILI9340_WHITE);
    
    tft_drawLine(305,150,305,80,ILI9340_WHITE);
    tft_drawLine(306,150,306,80,ILI9340_WHITE);
    
    tft_drawLine(265,55,305,80,ILI9340_WHITE);
    tft_drawLine(266,56,306,81,ILI9340_WHITE);
    tft_drawLine(267,57,307,82,ILI9340_WHITE);
    
    // init the port expander
    start_spi2_critical_section;
    
    // PortY on Expander ports as digital outputs
    mPortYSetPinsOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortY as inputs
    // note that bit 7 will be shift key input, 
    // separate from keypad
    mPortYSetPinsIn(BIT_4 | BIT_5 | BIT_6 | BIT_7);    //Set port as input
    mPortYEnablePullUp(BIT_4 | BIT_5 | BIT_6 | BIT_7);
    
    end_spi2_critical_section ;
    
    int press = 0;
    while(!press){
        for (i=0; i<4; i++) {
            start_spi2_critical_section;
            // scan each row active-low
            writePE(GPIOY, out_table[i]);
            //reading the port also reads the outputs
            keypad  = readPE(GPIOY);
            end_spi2_critical_section;
            // was there a key press?
            if((keypad & no_button) != no_button) {
                break;
            }
        }
        // search for keycode
        if (keypad > 0){ // then button is pushed
            for (i=0; i<24; i++){
                if (keytable[i]==keypad) break;
            }
            // if invalid, two button push, set to -1
            if (i==24) i=-1;
        }
        else i = -1; // no button pushed
        
        if(i==11){press=1;}
    }

    
    // === setup system wide interrupts  ========
    INTEnableSystemMultiVectoredInt();
    
    // write initial words
    tft_fillScreen(ILI9340_BLACK);
    sprintf(buffer, "Note Name: -"); 
    printLine(2,1, buffer, ILI9340_WHITE, ILI9340_BLACK);
    sprintf(buffer,"Tempo:");
    printLine(5,3, buffer, tempo_color, ILI9340_BLACK);
    sprintf(buffer,"   0 BPM");
    printLine(5, 5, buffer, tempo_color, ILI9340_BLACK);
    sprintf(buffer, "PRESS # TO ENTER TEMPO");
    printLine(2, 10, buffer, ILI9340_MAGENTA, ILI9340_BLACK);
    sprintf(buffer, "PRESS * TO CLEAR");
    printLine(2, 11, buffer, ILI9340_WHITE, ILI9340_BLACK);

    // round-robin scheduler for threads
    while (1) {
        PT_SCHEDULE(protothread_fft(&pt_fft));
        PT_SCHEDULE(protothread_met(&pt_met));
        PT_SCHEDULE(protothread_timer(&pt_timer));
        PT_SCHEDULE(protothread_key(&pt_key));
    }
} // main

// === end  ======================================================
