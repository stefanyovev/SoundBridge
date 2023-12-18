

    #define title "SoundBridge"

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <math.h>
    #include <windows.h>
    #include <portaudio.h>

    #define OK 0
    #define FAIL 1    
    #define PRINT printf
    
    void PaUtil_InitializeClock( void );
    double PaUtil_GetTime( void );
    double T0;
    
    #define SAMPLERATE 48000 // [samples/second]
    #define SAMPLESIZE sizeof(float)
    
    #define NOW ((long)(ceil((PaUtil_GetTime()-T0)*SAMPLERATE))) // [samples]   
     
    #define STATSCOUNT 100


    struct stat {                                                  // STAT
        long t, avail, frameCount; };

    struct port {
        PaDeviceInfo *device_info;
        PaStream *stream;
        int channels_count;
        long t0, len;
        struct stat stats[STATSCOUNT];
        int stats_i; }

    INPORT, OUTPORT;
    
    float *canvas;    
    int *map;
    long cursor;

    int msize = 100000;                            // channel memory size [samples]


    PaStreamCallbackResult device_tick(                            // RECEIVE
        float **input,
        float **output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo *timeInfo,
        PaStreamCallbackFlags statusFlags,
        void *userdata ){

        if( statusFlags )
            PRINT( "statusFlags: %d", statusFlags );

        if( input ){ PRINT("i");
         
            if( INPORT.t0 == 0 )
                INPORT.t0 = NOW;
                
            INPORT.len += frameCount; // commit
        }
        
        if( output ){ PRINT("o");
        
            if( OUTPORT.t0 == 0 )
                OUTPORT.t0 = NOW;
                
            OUTPORT.len += frameCount; // commit
        }

        return paContinue; }


    int start( int input_device_id, int output_device_id ){                            // +DEVICE        
        for( int i=1; i>-1; i-- ){
            PRINT( "starting %s ... \n", ( i ? "input" : "output" ) );
        
            int device_id = ( i ? input_device_id : output_device_id );
            const PaDeviceInfo *device_info = Pa_GetDeviceInfo( device_id );
            static PaStreamParameters params;
            PaStream **stream = ( i ? &(INPORT.stream) : &(OUTPORT.stream) );
            
            params.device = device_id;
            params.sampleFormat = paFloat32|paNonInterleaved;
            params.hostApiSpecificStreamInfo = 0;            
            params.suggestedLatency = ( i ? device_info->defaultLowInputLatency : device_info->defaultLowOutputLatency ); // "buffer size" [seconds]
            params.channelCount = ( i ? device_info->maxInputChannels : device_info->maxOutputChannels );
            
            PRINT( "opening stream ... \n" );
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
                    PRINT( "ERROR 1: %s \n ", Pa_GetErrorText( err ) );
                    return FAIL; }
                else {
                    const PaHostErrorInfo* herr = Pa_GetLastHostErrorInfo();
                    PRINT( "ERROR 2: %s \n ", herr->errorText );
                    return FAIL; }}
                    
            PRINT( "starting stream ... \n" );                    
            err = Pa_StartStream( *stream );
            if( err != paNoError ){
                PRINT( "ERROR 3: %s \n ", Pa_GetErrorText( err ) );
                return FAIL; }}

        PRINT( "ok \n " );
        return OK; }


    int main2( HANDLE handle );                                    // MAIN
    
    int main( int agrc, char* argv ){
        // SetProcessDPIAware();

        PRINT( "\n\t%s\n\n", title );

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

        CreateThread( 0, 0, &main2, 0, 0, 0 );

        char cmd[1000] = "";

        while( 1 ){

            fflush( stdout );
            PRINT( "\t] " );
            gets( cmd );

            if( strcmp( cmd, "q" ) == 0 )
                return OK; }}


    // ------------------------------------------------------------------------------------------------------------ //


    const int width = 600;
    const int height = 700;
    const int vw = 10000; // viewport width samples

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

                if( start( sd, dd ) == FAIL )
                    MessageBox( hwnd, "FAIL", "", MB_OK ); }}

        else if( msg == WM_CLOSE )
            DestroyWindow( hwnd );
        else if( msg == WM_DESTROY )
            PostQuitMessage( 0 );
        return DefWindowProc( hwnd, msg, wParam, lParam ); }


    int main2( HANDLE handle ){                                    // MAIN2
        HINSTANCE hInstance = GetModuleHandle(0);

        WNDCLASSEX wc;
        memset( &wc, 0, sizeof(wc) );
        wc.cbSize = sizeof(wc);
        wc.hInstance = hInstance;
        wc.lpfnWndProc = WndProc;
        wc.lpszClassName = "mainwindow";
        wc.hbrBackground = COLOR_WINDOW; //CreateSolidBrush( RGB(64, 64, 64) );

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
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight =  -(height-70);         // Order pixels from top to bottom
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;             // last byte not used, 32 bit for alignment
        bmi.bmiHeader.biCompression = BI_RGB;

        hdc = GetDC( hwnd );
        hbmp = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, &pixels, 0, 0 );
        hdcMem = CreateCompatibleDC( hdc );
        SelectObject( hdcMem, hbmp );

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
            else {
                // drawing
            }
        }
        return 0; }
