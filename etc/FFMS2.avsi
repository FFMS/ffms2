# FFmpegSource 1.21 syntax compatibility
# Created by TheFluff

function FFmpegSource2(string source, int "vtrack", int "atrack", bool "cache", \
	string "cachefile", int "fpsnum", int "fpsden", string "pp", int "threads", \
	string "timecodes", int "seekmode", bool "overwrite", int "width", int "height", \
	string "resizer", string "colorspace", int "rffmode", int "adjustdelay") {
	
	vtrack 		= default(vtrack,-1)
	atrack		= default(atrack,-2)
	cache		= default(cache,true)
	cachefile	= default(cachefile,source+".ffindex")
	fpsnum		= default(fpsnum,-1)
	fpsden		= default(fpsden,1)
	pp		= default(pp,"")
	threads		= default(threads,-1)
	timecodes	= default(timecodes,"")
	seekmode	= default(seekmode,1)
	overwrite	= default(overwrite,false)
	width		= default(width,-1)
	height		= default(height,-1)
	resizer		= default(resizer,"BICUBIC")
	colorspace	= default(colorspace,"")
	rffmode		= default(rffmode,0)
	adjustdelay	= default(adjustdelay,-1)

	
	((cache == true) && (atrack <= -2)) ? ffindex(source=source, cachefile=cachefile, \
		indexmask=0, overwrite=overwrite) : (cache == true) ? ffindex(source=source, \
		cachefile=cachefile, indexmask=-1, overwrite=overwrite) : nop
	
	v = ffvideosource(source=source, track=vtrack, cache=cache, cachefile=cachefile, \
		fpsnum=fpsnum, fpsden=fpsden, pp=pp, threads=threads, timecodes=timecodes, \
		seekmode=seekmode, rffmode=rffmode, width=width, height=height, resizer=resizer, \
		colorspace=colorspace)
	
	a = (atrack <= -2) ? blankclip(audio_rate=0) : ffaudiosource(source=source, \
		track=atrack, cache=cache, cachefile=cachefile, adjustdelay=adjustdelay)
	
	return audiodubex(v,a)
}

function FFImageSource(string source, int "width", int "height", string "resizer", string "colorspace") {

	width		= default(width,-1)
	height		= default(height,-1)
	resizer		= default(resizer,"BICUBIC")
	colorspace	= default(colorspace,"")

	return FFVideoSource(source, cache=false, seekmode=-1, width=width, height=height, resizer=resizer, colorspace=colorspace)
}

function FFFormatTime(int ms) {

	s = ms / 1000
	ms = ms % 1000
	m = s / 60
	s = s % 60
	h = m / 60
	m = m % 60

	return string(h) + ":" + string(m,"%02.0f") + ":" + string(s,"%02.0f") + "." + string(ms,"%03.0f")
}

function FFInfo(clip c, bool "framenum", bool "frametype", bool "cfrtime", bool "vfrtime") {

	framenum	= default(framenum,true)
	frametype	= default(frametype,true)
	cfrtime		= default(cfrtime,true)
	vfrtime		= default(vfrtime,true)

	global fftempclip = c
	fftempclip = frameevaluate(fftempclip, """ fftempstring = "" """)

	fftempclip = framenum ? frameevaluate(fftempclip, """fftempstring = fftempstring + "Frame Number: " + string(current_frame) + " of " + string(fftempclip.framecount) + "\n" """, after_frame=true) : fftempclip
	fftempclip = frametype ? frameevaluate(fftempclip, """fftempstring = fftempstring + "Picture Type: " + chr(FFPICT_TYPE) + "\n" """, after_frame=true) : fftempclip

	fftempclip = cfrtime ? frameevaluate(fftempclip, """fftempstring = fftempstring + "CFR Time: " + FFFormatTime(int((current_frame * 1000) / fftempclip.framerate)) + "\n" """, after_frame=true) : fftempclip
	fftempclip = vfrtime ? frameevaluate(fftempclip, """fftempstring = fftempstring + "VFR Time: " + FFFormatTime(FFVFR_TIME) + "\n" """, after_frame=true) : fftempclip

	return scriptclip(fftempclip, "subtitle(fftempclip, fftempstring, lsp = 1)", after_frame=true)
}