# FFmpegSource 1.21 syntax compatibility
# Created by TheFluff

function FFmpegSource2(string source, int "vtrack", int "atrack", bool "cache", \
	string "cachefile", int "fpsnum", int "fpsden", int "threads", \
	string "timecodes", int "seekmode", bool "overwrite", int "width", int "height", \
	string "resizer", string "colorspace", int "rffmode", int "adjustdelay", \
	bool "utf8", string "varprefix") {
	
	vtrack 		= default(vtrack,-1)
	atrack		= default(atrack,-2)
	cache		= default(cache,true)
	cachefile	= default(cachefile,source+".ffindex")
	fpsnum		= default(fpsnum,-1)
	fpsden		= default(fpsden,1)
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
	utf8		= default(utf8,false)
	varprefix	= default(varprefix, "")
	
	((cache == true) && (atrack <= -2)) ? ffindex(source=source, cachefile=cachefile, \
		indexmask=0, overwrite=overwrite, utf8=utf8) : (cache == true) ? ffindex(source=source, \
		cachefile=cachefile, indexmask=-1, overwrite=overwrite, utf8=utf8) : nop
	
	v = ffvideosource(source=source, track=vtrack, cache=cache, cachefile=cachefile, \
		fpsnum=fpsnum, fpsden=fpsden, threads=threads, timecodes=timecodes, \
		seekmode=seekmode, rffmode=rffmode, width=width, height=height, resizer=resizer, \
		colorspace=colorspace, utf8=utf8, varprefix=varprefix)
	
	a = (atrack <= -2) ? blankclip(audio_rate=0) : ffaudiosource(source=source, \
		track=atrack, cache=cache, cachefile=cachefile, adjustdelay=adjustdelay, \
		utf8=utf8, varprefix=varprefix)
	
	return audiodubex(v,a)
}

function FFImageSource(string source, int "width", int "height", string "resizer", \
	string "colorspace", bool "utf8", string "varprefix") {

	width		= default(width,-1)
	height		= default(height,-1)
	resizer		= default(resizer,"BICUBIC")
	colorspace	= default(colorspace,"")
	utf8		= default(utf8,false)
	varprefix	= default(varprefix,"")

	return FFVideoSource(source, cache=false, seekmode=-1, width=width, height=height, \
		resizer=resizer, colorspace=colorspace, utf8=utf8, varprefix=varprefix)
}

function FFCopyrightInfringement(string source) {
################################################################
# Violates copyright
# * With audio
# * No annoying lawyers
# * Simple syntax
# * Do not use on Britney Spears' music videos or sex tapes
#
# And whatever you do:
#   DO NOT TELL NEURON2 THAT YOU USED THIS FUNCTION
################################################################
	FFIndex(source=source)
	return audiodubex(FFVideoSource(source=source), FFAudioSource(source=source))
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

function FFInfo(clip c, bool "framenum", bool "frametype", bool "cfrtime", bool "vfrtime", string "varprefix") {

	framenum	= default(framenum,true)
	frametype	= default(frametype,true)
	cfrtime		= default(cfrtime,true)
	vfrtime		= default(vfrtime,true)
	varprefix	= default(varprefix, FFVAR_PREFIX)

	c.frameevaluate("""
		fftempstring = "" 
		varprefix = """" + varprefix + """"""")

	framenum ? frameevaluate("""fftempstring = fftempstring + "Frame Number: " + string(current_frame) + " of " + string(framecount()) + "\n" """, after_frame=true) : nop()
	frametype ? frameevaluate("""fftempstring = fftempstring + "Picture Type: " + chr(eval(varprefix + "FFPICT_TYPE")) + "\n" """, after_frame=true) : nop()

	cfrtime ? frameevaluate("""fftempstring = fftempstring + "CFR Time: " + FFFormatTime(round((current_frame * 1000) / framerate())) + "\n" """, after_frame=true) : nop()
	vfrtime ? frameevaluate("""fftempstring = fftempstring + "VFR Time: " + FFFormatTime(eval(varprefix + "FFVFR_TIME")) + "\n" """, after_frame=true) : nop()

	return scriptclip("subtitle(fftempstring, lsp = 1)", after_frame=true)
}