

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


    struct graph {
        POINT points[POINTSMAX];
        int full;
        long cursor;
        long min, max; };

    struct port {
        PaStream *stream;
        long t0, len;        
        struct graph graph; }

    INPORT, OUTPORT;


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

        if( input ){
        
            // write
        
            // stamp
            now = NOW;
            
            if( INPORT.t0 == 0 )
                INPORT.t0 = now;
                
            // commit
            INPORT.len += frameCount;
            
            // log
            if( INPORT.graph.full == 0 && INPORT.graph.cursor == 0 )
                PRINT( "profiling input \n" );
            INPORT.graph.points[INPORT.graph.cursor].x = now;
            INPORT.graph.points[INPORT.graph.cursor].y = INPORT.t0 + INPORT.len -frameCount -now; // avail before insert
            INPORT.graph.cursor++; // should not get > POINTSMAX now
            INPORT.graph.points[INPORT.graph.cursor].x = now;
            INPORT.graph.points[INPORT.graph.cursor].y = INPORT.t0 + INPORT.len -now; // avail now
            INPORT.graph.cursor++;
            if( INPORT.graph.cursor == POINTSMAX ){
                INPORT.graph.cursor = 0;

                long min = 9999;
                long max = -9999;
                for( int i=0; i<POINTSMAX; i++ ){
                    if( INPORT.graph.points[i].y < min )
                        min = INPORT.graph.points[i].y;
                    if( INPORT.graph.points[i].y > max )
                        max = INPORT.graph.points[i].y;
                }
                if( INPORT.graph.min != min ){
                    INPORT.graph.min = min;
                    PRINT( "input min_avail = %d \n", min );
                }
                if( INPORT.graph.max != max ){
                    INPORT.graph.max = max;
                    //PRINT( "input max_avail = %d \n", max );
                }

                if( INPORT.graph.full == 0 ){
                    INPORT.graph.full = 1;
                    PRINT( "profiled input \n" );
                }
            }
        }
        
        if( output ){ // PRINT("o");
        
            if( OUTPORT.t0 == 0 )
                OUTPORT.t0 = NOW;
                
            OUTPORT.len += frameCount; // commit
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
                    
            err = Pa_StartStream( *stream );
            if( err != paNoError ){
                PRINT( "ERROR 3: %s \n", Pa_GetErrorText( err ) );
                return FAIL; }
            
            struct port *p = ( i ? &INPORT : &OUTPORT );
            p->t0 = 0;
            p->len = 0;
            p->graph.cursor = 0;
            p->graph.full = 0;
            
            PaStreamInfo *stream_info = Pa_GetStreamInfo( *stream );            
            PRINT( "SampleRate %d \n", (int)round(stream_info->sampleRate) );
            PRINT( "FrameCount %d \n", (int)round( i ? (stream_info->inputLatency)*SAMPLERATE : (stream_info->outputLatency)*SAMPLERATE ));
            
        }

        PRINT( "ok. should be playing. \n" );
        return OK; }



    // ############################################################################################################ //


    const int width = 600;
    const int height = 700;
    const int WW = 574, HH = 200;

    HWND hwnd, hCombo1, hCombo2, hBtn;
    
    HDC hdc, hdcMem;
    HBITMAP hbmp;
    void ** pixels;
    
    RECT rc;
    MSG msg;
    BOOL done = FALSE;


    #define CMB1 (555)
    #define CMB2 (556)
    #define BTN1 (123)


    LRESULT CALLBACK WndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ){
        if( msg == WM_COMMAND ){
            if( LOWORD(wParam) == BTN1 ){

                int sd, dd;
                char*  txt[300];

                GetDlgItemText( hwnd, CMB1, txt, 255 );
                sscanf( txt, "  %3d", &sd );

                GetDlgItemText( hwnd, CMB2, txt, 255 );
                sscanf( txt, "  %3d", &dd );

                int ok = start( sd, dd );
                
                EnableWindow( hCombo1, ok );
                EnableWindow( hCombo2, ok );
                EnableWindow( hBtn, ok );
                
                 }}

        else if( msg == WM_CLOSE )
            DestroyWindow( hwnd );
        else if( msg == WM_DESTROY )
            PostQuitMessage( 0 );
        return DefWindowProc( hwnd, msg, wParam, lParam ); }


    int VPw = SAMPLERATE; // ViewPort width = 1 second in samples
    int VPh = 2000;       // ViewPort height = 2000 samples
    int VPx = 0;         // ViewPort pos x = now - width (rightmost points)
    int VPy = -500;     // ViewPort pos y = -VPh/2 so the absis comes vertically centered
    
    int Vw = 400;         // View width
    int Vh = 100;         // View height
    int Vx = 150;        // View pos x
    int Vy = 10;        // View pos y

    void transform_point( POINT *p ){
        double Qw = ((double)Vw)/((double)VPw);
        double Qh = ((double)Vh)/((double)VPh);        
        p->x = (long)round( (p->x - VPx) * Qw + Vx );
        p->y = (long)round( (p->y - VPy) * Qh + Vy );
        p->y = 2*Vy + Vh - p->y;
    }
    
    void draw(){
        memset( pixels, 128, WW*HH*4 );        
        GetClientRect( hwnd, &rc );        
        DrawText( hdcMem, (const char*) &console, -1, &rc, DT_LEFT );

        Rectangle( hdcMem, Vx, Vy, Vx+Vw, Vy+Vh );

        long now = NOW;
        VPx = now - VPw;
        
        POINT absis_p1 = {now - SAMPLERATE, 0};
        POINT absis_p2 = {now, 0};
        transform_point( &absis_p1 );
        transform_point( &absis_p2 );
        MoveToEx( hdcMem, absis_p1.x, absis_p1.y, 0 );
        LineTo( hdcMem, absis_p2.x, absis_p2.y );
        
        POINT p1 = {0, 0};
        POINT p2 = {0, 500};
        POINT p3 = {500, 0};
        transform_point( &p1 );
        transform_point( &p2 );
        transform_point( &p3 );
        MoveToEx( hdcMem, p1.x, p1.y, 0 );
        LineTo( hdcMem, p2.x, p2.y );
        LineTo( hdcMem, p3.x, p3.y );
        
        static POINT points[POINTSMAX-2]; // without the current stat; may be half written
        
        if( INPORT.graph.full ){
        
            int i = INPORT.graph.cursor +2; // leave the current stat; may be half written
            if( i == POINTSMAX ) // current was the last
                i = 0;

            int pi = 0;
            
            for( ;; ){
                
                points[pi++] = INPORT.graph.points[i++];
                
                i = i % POINTSMAX;
                if( i == INPORT.graph.cursor )
                    break;
            }

            for( pi=0; pi<POINTSMAX-2; pi++ )
                transform_point( points+pi );
            
            Polyline( hdcMem, points, POINTSMAX-2 );
            
        }
        
        // PRINT( "(%d,%d)(%d,%d)", points[pi-2].x, points[pi-2].y, points[pi-1].x, points[pi-1].x );
        BitBlt( hdc, 10, 70, WW, HH, hdcMem, 0, 0, SRCCOPY );
    }

    int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow ){                                    // MAIN2

        // SetProcessDPIAware();

        if( Pa_Initialize() ){
            PRINT( "ERROR: Pa_Initialize rubbish \n" );
            return FAIL; }
            
        if( Pa_GetDeviceCount() <= 0 ) {
            PRINT( "ERROR: No Devices Found \n" );
            return FAIL; }
            
        PaUtil_InitializeClock();
        T0 = PaUtil_GetTime();
        
        const PaVersionInfo *vi = Pa_GetVersionInfo();

        PRINT( "%s\n\n", vi->versionText );

        memset( &INPORT, 0, sizeof(INPORT) );
        memset( &OUTPORT, 0, sizeof(OUTPORT) );

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
        hCombo1 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 10, 490, 8000, hwnd, CMB1, NULL, NULL);
        hCombo2 = CreateWindowEx( 0, "ComboBox", 0, WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST, 10, 40, 490, 8000, hwnd, CMB2, NULL, NULL);
        hBtn = CreateWindowEx( 0, "Button", "Play >", WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_DEFPUSHBUTTON, 507, 10, 77, 53, hwnd, BTN1, NULL, NULL);

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

        ShowWindow( hwnd, SW_SHOW );

        char str[1000], txt[100000];

        for( int i=0; i<Pa_GetDeviceCount(); i++ ){
            PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            strcpy( str, Pa_GetHostApiInfo( info->hostApi )->name );
            sprintf( txt, " %3d  /  %s  /  %s ", i, strstr( str, "Windows" ) ? str+8 : str, info->name );
            if( info->maxInputChannels ) SendMessage( hCombo1, CB_ADDSTRING, 0, txt );
            if( info->maxOutputChannels ) SendMessage( hCombo2, CB_ADDSTRING, 0, txt );
            SendMessage( hCombo1, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 );
            SendMessage( hCombo2, CB_SETCURSEL, (WPARAM)0, (LPARAM)0 ); }

        while( !done ){
            if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ){
                if( msg.message == WM_QUIT )
                    done = TRUE;
                else {
                    TranslateMessage( &msg );
                    DispatchMessage( &msg ); }}
            else 
                draw();
        }
        
        return 0; }
