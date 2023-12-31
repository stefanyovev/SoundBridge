

    #define title "SoundBridge"

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <math.h>
    #include <windows.h>
    #include <portaudio.h>

    #define OK 0
    #define FAIL 1    
    
    char console[1000] = "";
    
    void PRINT( char *format, ... ){
        // TODO: lock of the global str

        static int width = 80, height = 12;
        static int lines = 1, firstline_len = 0, lastline_len = 0;
        static int cursor = 0;

        char str[1000] = "";
        
        va_list( args );
        va_start( args, format );
        vsprintf( str, format, args );
        va_end( args );
        
        if( strlen( str ) == 0 )
            return;

        for( int i=0; ; ){
            console[cursor++] = str[i++];
            lastline_len ++;
            
            if( str[i] == '\n' ){
                lines ++;
                if( lines == 2 )
                    firstline_len = lastline_len;
                lastline_len = 0; }
            
            if( lines > height ){
                strcpy( console, console+firstline_len+1 );
                lines--;
                cursor -= firstline_len+1;
                for( firstline_len = 0; console[firstline_len] != '\n'; firstline_len ++ ); }

            if( lastline_len == width ){
                console[cursor++] = '\n';
                lines ++;
                if( lines == 2 )
                    firstline_len = lastline_len;
                lastline_len = 0; }

            if( i == strlen( str ) )
                break; }
        
        console[cursor] = 0; }

    char * status_string( PaStreamCallbackFlags flags ){
        static char str[99]; str[0] = 0;
        if( flags & paInputUnderflow ) strcat( str, " & Input Underflow" );
        if( flags & paInputOverflow ) strcat( str, " & Input Overflow" );
        if( flags & paOutputUnderflow ) strcat( str, " & Output Underflow" );
        if( flags & paOutputOverflow ) strcat( str, " & Output Overflow" );
        if( flags & paPrimingOutput ) strcat( str, " & Priming Output" );
        return str +3; }
    
    // ############################################################################################################ //

    void PaUtil_InitializeClock( void );
    double PaUtil_GetTime( void );
    double T0;
    
    #define SAMPLERATE 48000 // [samples/second]
    #define SAMPLESIZE sizeof(float)

    #define NOW ((long)(ceil((PaUtil_GetTime()-T0)*SAMPLERATE))) // [samples]   
     
    #define POINTSMAX 200  // should be even
    #define POINTSMIN 20
    
    #define DIFFSMAX 100
    
    #define MSIZE SAMPLERATE // keep one second


    struct graph {
        POINT points[POINTSMAX];
        int full;
        long cursor;
        int min_i, max_i; };

    struct port {
        int channels_count;
        PaStream *stream;
        long t0, len;        
        struct graph graph; }

    INPORT, OUTPORT;

    long cursor;
    
    struct diff {
        int mag;
        long count;
    }
    
    diffs[DIFFSMAX];
    
    int diffs_i;
    int diffs_full;


    float *canvas;
    
    struct out {
        int src_chan;
    }
    
    *map;


    void aftermath( int sel, long t, int avail_after, int frameCount ){
        char *portname = ( sel == 0 ? "input" : "output" );
        struct graph *g = ( sel == 0 ? &(INPORT.graph) : &(OUTPORT.graph) );
        
        // make a stat
        g->points[g->cursor].x = t;
        g->points[g->cursor].y = avail_after -frameCount; // avail before insert
        g->cursor++;
        
        g->points[g->cursor].x = t;
        g->points[g->cursor].y = avail_after;
        g->cursor++;
        
        // graph end ?
        if( g->cursor == POINTSMAX ){
            g->cursor = 0;
            if( g->full == 0 )
                g->full = 1;
        }

        // find min/max (every time for constant cpu usage and ease)
        int pi = 0;
        int gi = ( g->full ? g->cursor : 0 );
        int count = ( g->full ? POINTSMAX : g->cursor );
        int min = 999999;
        int max = -999999;
        for( int i=0; i<count; i++ ){
            if( g->points[gi].y < min ){
                min = g->points[gi].y;
                g->min_i = gi;
            }
            if( g->points[gi].y > max ){
                max = g->points[gi].y;
                g->max_i = gi;
            }
            gi ++;
            gi %= POINTSMAX;
        }        

        // cursor ?
        if( (INPORT.graph.full || INPORT.graph.cursor > POINTSMIN) && (OUTPORT.graph.full || OUTPORT.graph.cursor > POINTSMIN) ){
        
            long new_cursor_candidate = 
                OUTPORT.t0 -INPORT.t0 // output.t0 as input lane address
                +OUTPORT.len // where is output end on the input
                +INPORT.graph.points[INPORT.graph.min_i].y -OUTPORT.graph.points[OUTPORT.graph.max_i].y; // our latency
                
            if( new_cursor_candidate >= 0 ){
            
                if( cursor == -1 ){
                    cursor = new_cursor_candidate;
                    PRINT( "curosr initialized (%d) \n", cursor );
                    
                } else if( cursor - new_cursor_candidate != 0 ){
                    // push a diff to think about
                    long diff = cursor - new_cursor_candidate;
                    if( diff == diffs[diffs_i].mag ){
                        diffs[diffs_i].count ++;
                    } else {
                        diffs_i ++;
                        if( diffs_i == DIFFSMAX ){
                            diffs_i = 0;
                            diffs_full = 1;
                        }
                        diffs[diffs_i].mag = diff;
                        diffs[diffs_i].count = 1;
                    }
                }
            }
        }
    }

    void correct_cursor_if_necessary(){
        if( diffs_full || diffs_i > 0 ){
            long diffs_sum = 0;
            long diffs_total = 0;
            int di = ( diffs_full ? diffs_i : 0 );
            int count = ( diffs_full ? DIFFSMAX : diffs_i );
            for( int i=0; i<count; i++ ){
                diffs_sum += diffs[di].mag * diffs[di].count;
                diffs_total += diffs[di].count;
                di ++; di %= DIFFSMAX;
            }
            if( diffs_sum != 0 ){ // may cancel each other
                double diffs_avg = ((double)diffs_sum)/((double)diffs_total);
                if( diffs_avg > 0 )
                    diffs_sum = (long)( floor( diffs_avg ) );
                else
                    diffs_sum = (long)( ceil( diffs_avg ) );
                if( diffs_sum != 0 ){ // may round to zero
                    cursor -= diffs_sum;
                    PRINT( "correction of [ %d ] samples \n", diffs_sum );
                    diffs_full = diffs_i = 0;
                }
            }
        }
    }

    PaStreamCallbackResult device_tick(                            // RECEIVE
        float **input,
        float **output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo *timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userdata ){
        
        long now;

        if( statusFlags )
            PRINT( "status: %s \n", status_string( statusFlags ) );
        
        if( input && output )
            PRINT( "strange \n" );

        if( input ){ // PRINT("i");
        
            // write
            int ofs = INPORT.len % MSIZE;
            for( int i=0; i<INPORT.channels_count; i++ )
                for( int j=0; j<3; j++ )
                    memcpy( canvas +i*MSIZE*4 +j*MSIZE +ofs, input[i], frameCount*SAMPLESIZE );
        
            // stamp
            now = NOW;
            
            if( INPORT.t0 == -1 )
                INPORT.t0 = now;
                
            // commit
            INPORT.len += frameCount;
            
            // log
            aftermath( 0, now, INPORT.t0 + INPORT.len -now, frameCount );
        }
        
        if( output ){ // PRINT("o");

            if( cursor > -1 ){
                // copy 
                for( int i=0; i<OUTPORT.channels_count; i++ ){
                    if( map[i].src_chan == -1 ) {
                        memset( output[i], 0, frameCount*SAMPLESIZE );
                        continue;
                    }
                    int ofs = cursor % MSIZE;
                    memcpy( output[i], canvas + map[i].src_chan*MSIZE*4 +MSIZE +ofs, frameCount*SAMPLESIZE );
                }
                
                if( cursor + frameCount > INPORT.len )
                    PRINT( "glitch %d \n", cursor -INPORT.len );
                cursor += frameCount;
            }

            // stamp
            now = NOW;
            
            if( OUTPORT.t0 == -1 )
                OUTPORT.t0 = now;

            // commit
            OUTPORT.len += frameCount;

            // log
            aftermath( 1, now, OUTPORT.t0 + OUTPORT.len -now, frameCount );
        }

        return paContinue; }


    int start( int input_device_id, int output_device_id ){
        for( int i=1; i>-1; i-- ){
            PRINT( "starting %s %d ... \n", ( i ? "input" : "output" ), ( i ? input_device_id : output_device_id ) );
        
            int device_id = ( i ? input_device_id : output_device_id );
            const PaDeviceInfo *device_info = Pa_GetDeviceInfo( device_id );
            static PaStreamParameters params;
            PaStream **stream = ( i ? &(INPORT.stream) : &(OUTPORT.stream) );
            
            params.device = device_id;
            params.sampleFormat = paFloat32|paNonInterleaved;
            params.hostApiSpecificStreamInfo = 0;            
            params.suggestedLatency = ( i ? device_info->defaultLowInputLatency : device_info->defaultLowOutputLatency ); // "buffer size" [seconds]
            params.channelCount = ( i ? device_info->maxInputChannels : device_info->maxOutputChannels );
            
            PaError err = Pa_OpenStream(
                stream,
                i ? &params : 0,
                i ? 0 : &params,
                SAMPLERATE,
                paFramesPerBufferUnspecified,
                paClipOff | paDitherOff | paPrimeOutputBuffersUsingStreamCallback, // paNeverDropInput ?
                &device_tick,
                0 );

            if( err != paNoError ){
                if( err != paUnanticipatedHostError ) {
                    PRINT( "ERROR 1: %s \n", Pa_GetErrorText( err ) );
                    return FAIL; }
                else {
                    const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
                    PRINT( "ERROR 2: %s \n", herr->errorText );
                    return FAIL; }}
            
            if( i ){
                INPORT.channels_count = device_info->maxInputChannels;
                PRINT( "%d channels \n", INPORT.channels_count );
                canvas = malloc( INPORT.channels_count * MSIZE*4 * SAMPLESIZE );
                if( !canvas )
                    PRINT( "ERROR: could not allocate memory" );
            } else {
                OUTPORT.channels_count = device_info->maxOutputChannels;
                PRINT( "%d channels \n", OUTPORT.channels_count );
                map = malloc( OUTPORT.channels_count * sizeof(struct out) );
                if( !map )
                    PRINT( "ERROR: could not allocate memory" );
                for( int i=0; i<OUTPORT.channels_count; i++ )
                    map[i].src_chan = -1;
            }

            err = Pa_StartStream( *stream );
            if( err != paNoError ){
                PRINT( "ERROR 3: %s \n", Pa_GetErrorText( err ) );
                return FAIL; }

            PaStreamInfo *stream_info = Pa_GetStreamInfo( *stream );            
            PRINT( "%d / %d \n",
                (int)round(stream_info->sampleRate),
                (int)round( i ? (stream_info->inputLatency)*SAMPLERATE : (stream_info->outputLatency)*SAMPLERATE ) );
        }

        // done
        PRINT( "both started. \n" );
        return OK; }



    // ############################################################################################################ //


    const int width = 600;
    const int height = 700;
    const int WW = 574, HH = 200;

    HWND hwnd;
    HDC hdc, hdcMem;
    HBITMAP hbmp;
    void ** pixels;
    
    RECT rc;
    MSG msg;
    BOOL done = FALSE;


    #define CMB1 (555)
    #define CMB2 (556)
    #define BTN1 (123)
    
    #define LB1 (110)
    #define CB1 (220)

    HWND hCombo1, hCombo2, hBtn;
    HWND cbs[10];

    int VPw = SAMPLERATE; // ViewPort width = 1 second in samples
    int VPh = 3000;       // ViewPort height = 2000 samples
    int VPx = 0;         // ViewPort pos x = now - width (rightmost points)
    int VPy = -500;     // ViewPort pos y = -VPh/2 so the absis comes vertically centered
    
    int Vw = 400;         // View width
    int Vh = 100;         // View height
    int Vx = 150;        // View pos x
    int Vy = 15;        // View pos y

    long now;
    
    void transform_point( POINT *p ){
        double Qw = ((double)Vw)/((double)VPw);
        double Qh = ((double)Vh)/((double)VPh);        
        p->x = (long)round( (p->x - VPx) * Qw + Vx );
        p->y = (long)round( (p->y - VPy) * Qh + Vy );
        p->y = 2*Vy + Vh - p->y;
    }
    
    void draw_graph( HDC ddc, struct graph *g ){
        static POINT points[POINTSMAX];
        if( g->min_i > -1 ){            
            int pi = 0;
            int gi = ( g->full ? g->cursor : 0 );
            int count = ( g->full ? POINTSMAX : g->cursor );
            for( int i=0; i<count; i++ ){
                points[pi++] = g->points[gi++];
                transform_point( points+pi-1 );
                gi %= POINTSMAX;
            }        
            Polyline( ddc, points, count );
        }
        POINT min_left = { now - SAMPLERATE, g->points[g->min_i].y };
        POINT min_right = { now, g->points[g->min_i].y };
        transform_point( &min_left );
        transform_point( &min_right );
        MoveToEx( ddc, min_left.x, min_right.y, 0 );
        LineTo( ddc, min_right.x, min_right.y );
        POINT max_left = { now - SAMPLERATE, g->points[g->max_i].y };
        POINT max_right = { now, g->points[g->max_i].y };
        transform_point( &max_left );
        transform_point( &max_right );
        MoveToEx( ddc, max_left.x, max_right.y, 0 );
        LineTo( ddc, max_right.x, max_right.y );
    }
    
    void draw(){
        memset( pixels, 128, WW*HH*4 );        
        GetClientRect( hwnd, &rc );        
        DrawText( hdcMem, (const char*) &console, -1, &rc, DT_LEFT );

        Rectangle( hdcMem, Vx, Vy, Vx+Vw, Vy+Vh );

        // stamp
        now = NOW + (SAMPLERATE/100); // show 10ms of the future
        VPx = now - VPw; // set viewport position
        
        // absis
        POINT absis_p1 = {now - SAMPLERATE, 0};
        POINT absis_p2 = {now, 0};
        transform_point( &absis_p1 );
        transform_point( &absis_p2 );
        MoveToEx( hdcMem, absis_p1.x, absis_p1.y, 0 );
        LineTo( hdcMem, absis_p2.x, absis_p2.y );
        
        // test impulse
        POINT p1 = {0, 0};
        POINT p2 = {0, 500};
        POINT p3 = {500, 0};
        transform_point( &p1 );
        transform_point( &p2 );
        transform_point( &p3 );
        MoveToEx( hdcMem, p1.x, p1.y, 0 );
        LineTo( hdcMem, p2.x, p2.y );
        LineTo( hdcMem, p3.x, p3.y );
        
        // graphs
        draw_graph( hdcMem, &(INPORT.graph) );
        draw_graph( hdcMem, &(OUTPORT.graph) );
        
        // commit
        BitBlt( hdc, 10, 460, WW, HH, hdcMem, 0, 0, SRCCOPY );
    }


    LRESULT CALLBACK WndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ){
        if( msg == WM_COMMAND ){
            if( LOWORD(wParam) == BTN1 ){

                int sd, dd;
                char txt[300];

                GetDlgItemText( hwnd, CMB1, txt, 255 );
                sscanf( txt, "  %3d", &sd );

                GetDlgItemText( hwnd, CMB2, txt, 255 );
                sscanf( txt, "  %3d", &dd );

                if( start( sd, dd ) == OK ){
                    for( int i=0; i<OUTPORT.channels_count; i++ )
                        map[i].src_chan = i % 2; // LR LR LR ..
                    EnableWindow( hCombo1, 0 );
                    EnableWindow( hCombo2, 0 );
                    EnableWindow( hBtn, 0 );
                    for( int i=0; i<OUTPORT.channels_count; i++ ){
                        if( i <= 10 ){
                            EnableWindow( cbs[i], 1 );
                            for( int j=0; j<INPORT.channels_count; j++ ){
                                char str[3] = "nn";
                                if( j+1 != 10 ) { str[0] = 48+j+1; str[1] = 0; }
                                else { str[0] = 49; str[1] = 48; str[2] = 0;}
                                SendMessage( cbs[i], CB_ADDSTRING, 0, str );
                                SendMessage( cbs[i], CB_SETCURSEL, (WPARAM)map[i].src_chan+1, (LPARAM)0 );
                            }
                        }
                    }
                }
            } else if( (LOWORD(wParam) >= CB1) && LOWORD(wParam) <= CB1+10 && CBN_SELCHANGE == HIWORD(wParam) ){
            
                int out = LOWORD(wParam) - CB1;
                
                char  txt[20];
                GetDlgItemText( hwnd, CB1+out, txt, 20 );
                
                int chan = txt[0]-48-1;
                if( chan < 10 ) {
                    map[out].src_chan = chan;
                    PRINT( "mapped out %d to in %d\n", out+1, chan+1 ); }
                else {
                    map[out].src_chan = -1;
                    PRINT( "muted out %d \n", out+1 ); }
                
                
            }
         }

        else if( msg == WM_CLOSE )
            DestroyWindow( hwnd );
        else if( msg == WM_DESTROY )
            PostQuitMessage( 0 );
        return DefWindowProc( hwnd, msg, wParam, lParam ); }


    int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow ){                                    // MAIN2

        // SetProcessDPIAware();

        if( Pa_Initialize() )
            PRINT( "ERROR: Could not initialize PortAudio. \n" );
        if( Pa_GetDeviceCount() <= 0 )
            PRINT( "ERROR: No Devices Found. \n" );
            
        PRINT( "%s\n\n", Pa_GetVersionInfo()->versionText );            
        
        PaUtil_InitializeClock();
        T0 = PaUtil_GetTime();

        // globals
        struct port* ports [2] = { &INPORT, &OUTPORT };
        for( int i=0; i<2; i++ ){
            struct port *p = ports[i];
            memset( p, 0, sizeof(struct port) );
            memset( p, 0, sizeof(struct port) );
            p->t0 = -1; // invalid
            p->len = 0;
            p->graph.cursor = 0;
            p->graph.full = 0;
            p->graph.min_i = -1; // invalid
            p->graph.max_i = -1; // invalid
        }
        cursor = -1; // invalid
        diffs_full = 0;
        diffs_i = 0;

        // UI
        WNDCLASSEX wc;
        memset( &wc, 0, sizeof(wc) );
        wc.cbSize = sizeof(wc);
        wc.hInstance = hInstance;
        wc.lpfnWndProc = WndProc;
        wc.lpszClassName = "mainwindow";
        wc.hbrBackground = COLOR_WINDOW; //CreateSolidBrush( RGB(64, 64, 64) );
        wc.hCursor = LoadCursor( 0, IDC_ARROW );

        if( !RegisterClassEx(&wc) ){
            MessageBox( 0, "Failed to register window class.", "Error", MB_OK );
            return 0; }

        hwnd = CreateWindowEx( WS_EX_APPWINDOW, "mainwindow", title, WS_MINIMIZEBOX | WS_SYSMENU | WS_POPUP | WS_CAPTION, 300, 200, width, height, 0, 0, hInstance, 0 );

        BITMAPINFO bmi;
        memset( &bmi, 0, sizeof(bmi) );
        bmi.bmiHeader.biSize = sizeof(bmi);
        bmi.bmiHeader.biWidth = WW;
        bmi.bmiHeader.biHeight =  -HH;         // Order pixels from top to bottom
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;             // last byte not used, 32 bit for alignment
        bmi.bmiHeader.biCompression = BI_RGB;

        hdc = GetDC( hwnd );
        hbmp = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &pixels, 0, 0 );
        hdcMem = CreateCompatibleDC( hdc );
        SelectObject( hdcMem, hbmp );
        SetBkMode( hdcMem, TRANSPARENT );

        hCombo1 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 10, 490, 8000, hwnd, CMB1, NULL, NULL);
        hCombo2 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 40, 490, 8000, hwnd, CMB2, NULL, NULL);
        hBtn = CreateWindowEx( 0, "Button", "Play >", WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_DEFPUSHBUTTON, 507, 10, 77, 53, hwnd, BTN1, NULL, NULL);

        for( int i=0; i<10; i++ ){
            char str[7] = "out nn";
            if( i+1 != 10 ) {
                str[4] = 48+i+1; str[5] = 0; }
            else {
                str[4] = 49; str[5] = 48; str[6] = 0;}
            CreateWindowEx( 0, "static", str, WS_VISIBLE|WS_CHILD, 50, 105+i*30, 50, 80, hwnd, LB1+i, NULL, NULL);
            cbs[i] = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 100, 100+i*30, 90, 80, hwnd, CB1+i, NULL, NULL);
            SendMessage( cbs[i], CB_ADDSTRING, 0, "None");
            SendMessage( cbs[i], CB_SETCURSEL, (WPARAM)0, (LPARAM)0 );
            EnableWindow( cbs[i], 0 );
        }

        ShowWindow( hwnd, SW_SHOW );

        // populate device dropdowns
        char str[1000], txt[100000];
        for( int i=0; i<Pa_GetDeviceCount(); i++ ){
            PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            strcpy( str, Pa_GetHostApiInfo( info->hostApi )->name );
            sprintf( txt, " %3d  /  %s  /  %s ", i, strstr( str, "Windows" ) ? str+8 : str, info->name );
            if( info->maxInputChannels ) SendMessage( hCombo1, CB_ADDSTRING, 0, txt );
            if( info->maxOutputChannels ) SendMessage( hCombo2, CB_ADDSTRING, 0, txt );
            SendMessage( hCombo1, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 );
            SendMessage( hCombo2, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 ); }

        // loop
        double last_correction_time = PaUtil_GetTime(); // queryperformancecounter de-facto
        // TODO: use NOW; get once in the loop; give it to draw
        while( !done ){
            if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ){
                if( msg.message == WM_QUIT )
                    done = TRUE;
                else {
                    TranslateMessage( &msg );
                    DispatchMessage( &msg ); }}
            else if( PaUtil_GetTime() - last_correction_time > 1.0 ){ // once per second
                correct_cursor_if_necessary();
                last_correction_time = PaUtil_GetTime();
            }
            else 
                draw();
        }
        
        return 0; }
