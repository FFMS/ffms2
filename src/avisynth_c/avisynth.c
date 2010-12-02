//  Copyright (c) 2010 FFmpegSource Project
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "ffms.h"
#include "avs_common.h"
#include "ff_filters.h"

/* set the correct entry point names for each platform, so things work smoothly.
 * these would likely need to be corrected for MSVC...
 * but as MSVC can't compile this code, pretend it doesn't exist. */
#ifndef _WIN64
#define AvisynthPluginInit2 _AvisynthPluginInit2
#endif

static const char* default_cache_file( const char *src, const char *cache_file )
{
    if( !strcmp( cache_file, "" ) )
    {
        char *def_cache = malloc( strlen( src ) + 9 );
        if( !def_cache )
            return NULL;
        strcpy( def_cache, src );
        strcat( def_cache, ".ffindex" );
        cache_file = def_cache;
    }
    return cache_file;
}

static int get_num_logical_cpus()
{
    SYSTEM_INFO SI;
    GetSystemInfo(&SI);
    return SI.dwNumberOfProcessors;
}

static AVS_Value AVSC_CC create_FFIndex( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_Init( avs_to_ff_cpu_flags( ffms_avs_lib->avs_get_cpu_flags( env ) ), as_bool( as_elt( args, 7 ), 0 ) );
    init_ErrorInfo( ei );

    AVS_Value elt0 = as_elt( args, 0 );
    if( !avs_is_string( elt0 ) )
        return avs_new_value_error( "FFIndex: No source specified" );

    const char *src = as_string( elt0, NULL );
    const char *cache_file = as_string( as_elt( args, 1 ), "" );
    int index_mask = as_int( as_elt( args, 2 ), -1 );
    int dump_mask = as_int( as_elt( args, 3 ), 0 );
    const char *audio_file = as_string( as_elt( args, 4 ), "%sourcefile%.%trackzn%.w64" );
    int err_handler = as_int( as_elt( args, 5 ), FFMS_IEH_IGNORE );
    char overwrite = as_bool( as_elt( args, 6 ), 0 );

    if( !(cache_file = default_cache_file( src, cache_file )) )
        return avs_new_value_error( "FFIndex: memory allocation failure" );

    if( !strcmp( audio_file, "" ) )
        return avs_new_value_error( "FFIndex: Specifying an empty audio filename is not allowed" );

    FFMS_Index *index = NULL;
    if( overwrite || !(index = FFMS_ReadIndex( cache_file, &ei )) )
    {
        if( !(index = FFMS_MakeIndex( src, index_mask, dump_mask, FFMS_DefaultAudioFilename,
                (void*)audio_file, 1, NULL, NULL, &ei )) )
            return avs_new_value_error( ffms_avs_sprintf( "FFIndex: %s", ei.Buffer ) );
        if( FFMS_WriteIndex( cache_file, index, &ei ) ) {
            FFMS_DestroyIndex( index );
            return avs_new_value_error( ffms_avs_sprintf( "FFIndex: %s", ei.Buffer ) );
        }
        FFMS_DestroyIndex( index );
        return avs_new_value_int( 2 - overwrite );
    }
    FFMS_DestroyIndex( index );
    return avs_new_value_int( 0 );
}

static AVS_Value AVSC_CC create_FFVideoSource( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_Init( avs_to_ff_cpu_flags( ffms_avs_lib->avs_get_cpu_flags( env ) ), as_bool( as_elt( args, 15 ), 0 ) );
    init_ErrorInfo( ei );

    AVS_Value elt0 = as_elt( args, 0 );
    if( !avs_is_string( elt0 ) )
        return avs_new_value_error( "FFVideoSource: No source specified" );

    const char *src = as_string( elt0, NULL );
    int track = as_int( as_elt( args, 1 ), -1 );
    char cache = as_bool( as_elt( args, 2 ), 1 );
    const char *cache_file = as_string( as_elt( args, 3 ), "" );
    int fps_num = as_int( as_elt( args, 4 ), -1 );
    int fps_den = as_int( as_elt( args, 5 ), 1 );
    const char *pp = as_string( as_elt( args, 6 ), "" );
    int threads = as_int( as_elt( args, 7 ), -1 );
    const char *timecodes = as_string( as_elt( args, 8 ), "" );
    int seek_mode = as_int( as_elt( args, 9 ), 1 );
    int rff_mode = as_int( as_elt( args, 10 ), 0 );
    int width = as_int( as_elt( args, 11 ), 0 );
    int height = as_int( as_elt( args, 12 ), 0 );
    const char *resizer = as_string( as_elt( args, 13 ), "BICUBIC" );
    const char *csp_name = as_string( as_elt( args, 14 ), "" );

    if( fps_den < 1 )
        return avs_new_value_error( "FFVideoSource: FPS denominator needs to be 1 or higher" );
    if( track <= -2 )
        return avs_new_value_error( "FFVideoSource: No video track selected" );
    if( seek_mode < -1 || seek_mode > 3 )
        return avs_new_value_error( "FFVideoSource: Invalid seekmode selected" );
    if( threads <= 0 )
        threads = get_num_logical_cpus();
    if( rff_mode < 0 || rff_mode > 2 )
        return avs_new_value_error( "FFVideoSource: Invalid RFF mode selected" );
    if( rff_mode > 0 && fps_num > 0 )
        return avs_new_value_error( "FFVideoSource: RFF modes may not be combined with CFR conversion" );
    if( !stricmp( src, timecodes ) )
        return avs_new_value_error( "FFVideoSource: Timecodes will overwrite the source" );

    if( !(cache_file = default_cache_file( src, cache_file )) )
        return avs_new_value_error( "FFVideoSource: memory allocation failure" );

    FFMS_Index *index = NULL;
    if( cache )
        index = FFMS_ReadIndex( cache_file, &ei );
    if( !index )
    {
        if( !(index = FFMS_MakeIndex( src, 0, 0, NULL, NULL, 1, NULL, NULL, &ei )) )
            return avs_new_value_error( ffms_avs_sprintf( "FFVideoSource: %s", ei.Buffer ) );

        if( cache )
            if( FFMS_WriteIndex( cache_file, index, &ei ) )
            {
                FFMS_DestroyIndex( index );
                return avs_new_value_error( ffms_avs_sprintf( "FFVideoSource: %s", ei.Buffer ) );
            }
    }

    if( track == -1 )
        track = FFMS_GetFirstIndexedTrackOfType( index, FFMS_TYPE_VIDEO, &ei );
    if( track < 0 )
        return avs_new_value_error( "FFVideoSource: No video track found" );

    if( strcmp( timecodes, "" ) )
        if( FFMS_WriteTimecodes( FFMS_GetTrackFromIndex( index, track ), timecodes, &ei ) )
        {
            FFMS_DestroyIndex( index );
            return avs_new_value_error( ffms_avs_sprintf( "FFVideoSource: %s", ei.Buffer ) );
        }

    AVS_Value video = FFVideoSource_create( env, src, track, index, fps_num, fps_den, pp, threads,
        seek_mode, rff_mode, width, height, resizer, csp_name );
    if( avs_is_error( video ) )
        FFMS_DestroyIndex( index );
    return video;
}

static AVS_Value AVSC_CC create_FFAudioSource( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_Init( avs_to_ff_cpu_flags( ffms_avs_lib->avs_get_cpu_flags( env ) ), as_bool( as_elt( args, 5 ), 0 ) );
    init_ErrorInfo( ei );

    if( !avs_is_string( as_elt( args, 0 ) ) )
        return avs_new_value_error( "FFAudioSource: No source specified" );

    const char *src = as_string( as_elt( args, 0 ), NULL );
    int track = as_int( as_elt( args, 1 ), -1 );
    char cache = as_bool( as_elt( args, 2 ), 1 );
    const char *cache_file = as_string( as_elt( args, 3 ), "" );
    int adjust_delay = as_int( as_elt( args, 4 ), -1 );

    if( track <= -2 )
        return avs_new_value_error( "FFAudioSource: No audio track selected" );

    if( !(cache_file = default_cache_file( src, cache_file )) )
        return avs_new_value_error( "FFAudioSource: memory allocation failure" );

    FFMS_Index *index = NULL;
    if( cache )
        index = FFMS_ReadIndex( cache_file, &ei );

    // Index needs to be remade if it is an unindexed audio track
    if( index && track >= 0 && track < FFMS_GetNumTracks( index ) &&
        FFMS_GetTrackType( FFMS_GetTrackFromIndex( index, track ) ) == FFMS_TYPE_AUDIO &&
        !FFMS_GetNumFrames( FFMS_GetTrackFromIndex( index, track ) ) )
    {
        FFMS_DestroyIndex( index );
        index = NULL;
    }

    // More complicated for finding a default track, reindex the file if at least one audio track exists
    if( index && FFMS_GetFirstTrackOfType( index, FFMS_TYPE_AUDIO, &ei ) >= 0 &&
        FFMS_GetFirstIndexedTrackOfType( index, FFMS_TYPE_AUDIO, &ei ) < 0 )
    {
        int i;
        for( i = 0; i < FFMS_GetNumTracks( index ); i++ )
            if( FFMS_GetTrackType( FFMS_GetTrackFromIndex( index, i ) ) == FFMS_TYPE_AUDIO )
            {
                FFMS_DestroyIndex( index );
                index = NULL;
                break;
            }
    }

    if( !index )
    {
        if( !(index = FFMS_MakeIndex( src, -1, 0, NULL, NULL, 1, NULL, NULL, &ei )) )
            return avs_new_value_error( ffms_avs_sprintf( "FFAudioSource: %s", ei.Buffer ) );

        if( cache )
            if( FFMS_WriteIndex( cache_file, index, &ei ) )
            {
                FFMS_DestroyIndex( index );
                return avs_new_value_error( ffms_avs_sprintf( "FFAudioSource: %s", ei.Buffer ) );
            }
    }

    if( track == -1 )
        track = FFMS_GetFirstIndexedTrackOfType( index, FFMS_TYPE_AUDIO, &ei );
    if( track < 0 )
        return avs_new_value_error( "FFAudioSource: No audio track found" );

    if( adjust_delay < -3 )
        return avs_new_value_error( "FFAudioSource: Invalid delay adjustment specified" );
    if( adjust_delay >= FFMS_GetNumTracks( index ) )
        return avs_new_value_error( "FFAudioSource: Invalid track to calculate delay from specified" );


    AVS_Value audio = FFAudioSource_create( env, src, track, index, adjust_delay );
    if( avs_is_error( audio ) )
        FFMS_DestroyIndex( index );
    return audio;
}

#ifdef FFMS_USE_POSTPROC
static AVS_Value AVSC_CC create_FFPP( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    AVS_Value child = as_elt( args, 0 );
    const char* pp = as_string( as_elt( args, 1 ), "" );
    return FFPP_create( env, child, pp );
}
#endif

static AVS_Value AVSC_CC create_SWScale( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    AVS_Value child = as_elt( args, 0 );
    int dst_width = as_int( as_elt( args, 1 ), 0 );
    int dst_height = as_int( as_elt( args, 2 ), 0 );
    const char* resizer = as_string( as_elt( args, 3 ), "BICUBIC" );
    const char* dst_pix_fmt = as_string( as_elt( args, 4 ), "" );
    return FFSWScale_create( env, child, dst_width, dst_height, resizer, dst_pix_fmt );
}

static AVS_Value AVSC_CC create_FFGetLogLevel( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{ return avs_new_value_int( FFMS_GetLogLevel() ); }

static AVS_Value AVSC_CC create_FFSetLogLevel( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_SetLogLevel( as_int( as_elt( args, 0 ), 0 ) );
    return avs_new_value_int( FFMS_GetLogLevel() );
}

/* the AVS loader for LoadCPlugin */
const char *AVSC_CC avisynth_c_plugin_init( AVS_ScriptEnvironment* env )
{
    /* load the avs library */
    if( ffms_load_avs_lib( env ) )
        return "Failure";
    ffms_avs_lib->avs_add_function( env, "FFIndex", "[source]s[cachefile]s[indexmask]i[dumpmask]i[audiofile]s[errorhandling]i[overwrite]b[utf8]b", create_FFIndex, 0 );
    ffms_avs_lib->avs_add_function( env, "FFVideoSource", "[source]s[track]i[cache]b[cachefile]s[fpsnum]i[fpsden]i[pp]s[threads]i[timecodes]s[seekmode]i[rffmode]i[width]i[height]i[resizer]s[colorspace]s[utf8]b", create_FFVideoSource, 0 );
    ffms_avs_lib->avs_add_function( env, "FFAudioSource", "[source]s[track]i[cache]b[cachefile]s[adjustdelay]i[utf8]b", create_FFAudioSource, 0 );
    ffms_avs_lib->avs_add_function( env, "SWScale", "c[width]i[height]i[resizer]s[colorspace]s", create_SWScale, 0 );
#ifdef FFMS_USE_POSTPROC
    ffms_avs_lib->avs_add_function( env, "FFPP", "c[pp]s", create_FFPP, 0 );
#endif
    ffms_avs_lib->avs_add_function( env, "FFGetLogLevel", "", create_FFGetLogLevel, 0 );
    ffms_avs_lib->avs_add_function( env, "FFSetLogLevel", "i", create_FFSetLogLevel, 0 );

    /* tell avs to call our cleanup method when it closes */
    ffms_avs_lib->avs_at_exit( env, ffms_free_avs_lib, 0 );
    return "FFmpegSource - The Second Coming V2.0 Final";
}

/* the x64 build of avisynth may or may not be wanting this function, depends on who built it */
#ifdef _WIN64
const char *AVSC_CC avisynth_c_plugin_init_s( AVS_ScriptEnvironment* env )
{ return avisynth_c_plugin_init( env ); }
#endif

/* the AVS loader for LoadPlugin.
 * Allows the conditional avs script logic:
 * ( LoadPlugin("ffms2.dll") == "Use LoadCPlugin" ) ? LoadCPlugin("ffms2.dll") : NOP()
 * to successfully load the plugin for both MSVC and MinGW versions */
const char * __stdcall AvisynthPluginInit2( void *Env )
{ return "Use LoadCPlugin"; }
