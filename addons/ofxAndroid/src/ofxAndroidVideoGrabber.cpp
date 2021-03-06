/*
 * ofxAndroidVideoGrabber.cpp
 *
 *  Created on: 09/07/2010
 *      Author: arturo
 */

#include "ofxAndroidVideoGrabber.h"
#include "ofxAndroidUtils.h"
#include <map>
#include "ofAppRunner.h"
#include "ofUtils.h"
#include "ofVideoGrabber.h"

struct ofxAndroidVideoGrabber::Data{
	bool bIsFrameNew;
	bool bGrabberInited;
	ofPixelFormat internalPixelFormat;
	ofPixels frontPixels;
	ofPixels backPixels;
	ofTexture texture;
	jfloatArray matrixJava;
	int cameraId;
	bool appPaused;
	bool newPixels;
	int attemptFramerate;
	jobject javaVideoGrabber;
	Data();
	~Data();
	void onAppPause();
	void onAppResume();
	void loadTexture();
	void reloadTexture();
};

map<int,weak_ptr<ofxAndroidVideoGrabber::Data>> & instances(){
	static auto * instances = new map<int,weak_ptr<ofxAndroidVideoGrabber::Data>>;
	return *instances;
}

static jclass getJavaClass(){
	JNIEnv *env = ofGetJNIEnv();

	jclass javaClass = env->FindClass("cc/openframeworks/OFAndroidVideoGrabber");

	if(javaClass==NULL){
		ofLogError("ofxAndroidVideoGrabber") << "couldn't find OFAndroidVideoGrabber java class";
	}

	return javaClass;
}

static void InitConvertTable();

ofxAndroidVideoGrabber::Data::Data()
:bIsFrameNew(false)
,bGrabberInited(false)
,cameraId(-1)
,appPaused(false)
,newPixels(false)
,attemptFramerate(-1)
,javaVideoGrabber(nullptr){
	JNIEnv *env = ofGetJNIEnv();

	jclass javaClass = getJavaClass();
	if(!javaClass) return;

	jmethodID videoGrabberConstructor = env->GetMethodID(javaClass,"<init>","()V");
	if(!videoGrabberConstructor){
		ofLogError("ofxAndroidVideoGrabber") << "initGrabber(): couldn't find OFAndroidVideoGrabber constructor";
		return;
	}

	javaVideoGrabber = env->NewObject(javaClass,videoGrabberConstructor);
	javaVideoGrabber = (jobject)env->NewGlobalRef(javaVideoGrabber);
	jmethodID javaCameraId = env->GetMethodID(javaClass,"getId","()I");
	if(javaVideoGrabber && javaCameraId){
		cameraId = env->CallIntMethod(javaVideoGrabber,javaCameraId);
	}else{
		ofLogError("ofxAndroidVideoGrabber") << "initGrabber(): couldn't get OFAndroidVideoGrabber camera id";
	}
	InitConvertTable();
	internalPixelFormat = OF_PIXELS_RGB;

	jfloatArray localMatrixJava = ofGetJNIEnv()->NewFloatArray(16);
	matrixJava = (jfloatArray) ofGetJNIEnv()->NewGlobalRef(localMatrixJava);
	ofAddListener(ofxAndroidEvents().unloadGL,this,&ofxAndroidVideoGrabber::Data::onAppPause);
	ofAddListener(ofxAndroidEvents().reloadGL,this,&ofxAndroidVideoGrabber::Data::onAppResume);
}

ofxAndroidVideoGrabber::Data::~Data(){
	JNIEnv *env = ofGetJNIEnv();
	jclass javaClass = getJavaClass();
	if(!javaClass) return;
	jmethodID javaOFDestructor = env->GetMethodID(javaClass,"release","()V");
	if(javaVideoGrabber && javaOFDestructor){
		env->CallVoidMethod(javaVideoGrabber,javaOFDestructor);
		env->DeleteGlobalRef(javaVideoGrabber);
	}
	if(matrixJava) ofGetJNIEnv()->DeleteGlobalRef(matrixJava);
	ofRemoveListener(ofxAndroidEvents().unloadGL,this,&ofxAndroidVideoGrabber::Data::onAppPause);
	ofRemoveListener(ofxAndroidEvents().reloadGL,this,&ofxAndroidVideoGrabber::Data::onAppResume);
}

void ofxAndroidVideoGrabber::Data::loadTexture(){

	if(!texture.texData.bAllocated) return;

	glGenTextures(1, (GLuint *)&texture.texData.textureID);

	glEnable(texture.texData.textureTarget);

	glBindTexture(texture.texData.textureTarget, (GLuint)texture.texData.textureID);

	glTexParameterf(texture.texData.textureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(texture.texData.textureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(texture.texData.textureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(texture.texData.textureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glDisable(texture.texData.textureTarget);

}

void ofxAndroidVideoGrabber::Data::reloadTexture(){

	JNIEnv *env = ofGetJNIEnv();
	if (!env) {
		ofLogError("ofxAndroidVideoGrabber") << "reloadTexture(): couldn't get environment using GetEnv()";
		return;
	}
	jmethodID javasetTexture = env->GetMethodID(getJavaClass(),"setTexture","(I)V");
	if(!javasetTexture){
		ofLogError("ofxAndroidVideoPlayer") << "reloadTexture(): couldn't get java setTexture for VideoPlayer";
		return;
	}
	env->CallVoidMethod(javaVideoGrabber,javasetTexture,texture.getTextureData().textureID);
}

ofxAndroidVideoGrabber::ofxAndroidVideoGrabber()
:data(new Data){
	if(data->cameraId!=-1){
		instances().insert(std::pair<int,weak_ptr<ofxAndroidVideoGrabber::Data>>(data->cameraId,data));
	}
}

ofxAndroidVideoGrabber::~ofxAndroidVideoGrabber(){
}

void ofxAndroidVideoGrabber::Data::onAppPause(){
	appPaused = true;
	texture.texData.textureID = 0;
	ofLogVerbose("ofxAndroidVideoGrabber") << "ofPauseVideoGrabbers(): releasing textures";
}

void ofxAndroidVideoGrabber::Data::onAppResume(){
	ofLogVerbose("ofxAndroidVideoGrabber") << "ofResumeVideoGrabbers(): trying to allocate textures";
	JNIEnv *env = ofGetJNIEnv();
	if(!env){
		ofLogError("ofxAndroidVideoGrabber") << "init grabber failed : couldn't get environment using GetEnv()";
		return;
	}
	jclass javaClass = getJavaClass();
	jmethodID javaInitGrabber = env->GetMethodID(javaClass,"initGrabber","(IIII)V");
	loadTexture();

	int texID= texture.texData.textureID;
	int w=texture.texData.width;
	int h=texture.texData.height;
	env->CallVoidMethod(javaVideoGrabber,javaInitGrabber,w,h,attemptFramerate,texID);
	ofLogVerbose("ofxAndroidVideoGrabber") << "ofResumeVideoGrabbers(): textures allocated";
	appPaused = false;
}
vector<ofVideoDevice> ofxAndroidVideoGrabber::listDevices() const{
	return vector<ofVideoDevice>();
}

bool ofxAndroidVideoGrabber::isFrameNew() const{
	return data->bIsFrameNew;
}

void ofxAndroidVideoGrabber::update(){
	if(data->appPaused){
		//ofLogWarning("ofxAndroidVideoGrabber") << "update(): couldn't grab frame, movie is paused";
		return;
	}
	//ofLogVerbose("ofxAndroidVideoGrabber") << "update(): updating camera";
	if(data->bGrabberInited && data->newPixels){
		//ofLogVerbose("ofxAndroidVideoGrabber") << "update(): new pixels";
		data->newPixels = false;
		data->bIsFrameNew = true;

		if(supportsTextureRendering()){
			jmethodID update = ofGetJNIEnv()->GetMethodID(getJavaClass(), "update", "()V");
			ofGetJNIEnv()->CallVoidMethod(data->javaVideoGrabber, update);

			jmethodID javaGetTextureMatrix = ofGetJNIEnv()->GetMethodID(getJavaClass(),"getTextureMatrix","([F)V");
			if(!javaGetTextureMatrix){
				ofLogError("ofxAndroidVideoPlayer") << "update(): couldn't get java javaGetTextureMatrix for VideoPlayer";
				return;
			}
			ofGetJNIEnv()->CallVoidMethod(data->javaVideoGrabber,javaGetTextureMatrix,data->matrixJava);
			jfloat * m = ofGetJNIEnv()->GetFloatArrayElements(data->matrixJava,0);

			ofMatrix4x4 vFlipTextureMatrix;
			vFlipTextureMatrix.scale(1,-1,1);
			vFlipTextureMatrix.translate(0,1,0);
			ofMatrix4x4 textureMatrix(m);
			data->texture.setTextureMatrix( vFlipTextureMatrix * textureMatrix );

			ofGetJNIEnv()->ReleaseFloatArrayElements(data->matrixJava,m,0);
		}
	}else{
		data->bIsFrameNew = false;
	}
}

void ofxAndroidVideoGrabber::close(){

}


ofTexture *	ofxAndroidVideoGrabber::getTexturePtr(){
	if(supportsTextureRendering()) return &data->texture;
	else return nullptr;
}

bool ofxAndroidVideoGrabber::supportsTextureRendering(){
	static bool supportsTexture = false;
	static bool supportChecked = false;
	if(!supportChecked){
		JNIEnv *env = ofGetJNIEnv();
		if(!env){
			ofLogError("ofxAndroidVideoGrabber") << "init grabber failed :  couldn't get environment using GetEnv()";
			return false;
		}

		jmethodID supportsTextureMethod = env->GetStaticMethodID(getJavaClass(),"supportsTextureRendering","()Z");
		supportsTexture = env->CallStaticBooleanMethod(getJavaClass(),supportsTextureMethod);
		supportChecked = true;
	}
	return supportsTexture;
}

bool ofxAndroidVideoGrabber::setup(int w, int h){
	if(data->bGrabberInited){
		ofLogError("ofxAndroidVideoGrabber") << "initGrabber(): camera already initialized";
		return false;
	}

	JNIEnv *env = ofGetJNIEnv();
	if(!env) return false;

	jclass javaClass = env->FindClass("cc/openframeworks/OFAndroidVideoGrabber");

	if(supportsTextureRendering()){
		ofLogNotice() << "initializing camera with external texture";
		ofTextureData td;
		td.width = w;
		td.height = h;
		td.tex_w = td.width;
		td.tex_h = td.height;
		td.tex_t = 1; // Hack!
		td.tex_u = 1;
		td.textureTarget = GL_TEXTURE_EXTERNAL_OES;
		td.glTypeInternal = GL_RGBA;
		td.bFlipTexture = false;

		// hack to initialize gl resources from outside ofTexture
		data->texture.texData = td;
		data->texture.texData.bAllocated = true;
		data->loadTexture();

		jmethodID javaInitGrabber = env->GetMethodID(javaClass,"initGrabber","(IIII)V");
		if(data->javaVideoGrabber && javaInitGrabber){
			env->CallVoidMethod(data->javaVideoGrabber,javaInitGrabber,w,h,data->attemptFramerate,data->texture.texData.textureID);
		}else{
			ofLogError("ofxAndroidVideoGrabber") << "initGrabber(): couldn't get OFAndroidVideoGrabber init grabber method";
			return false;
		}
	}else{
		jmethodID javaInitGrabber = env->GetMethodID(javaClass,"initGrabber","(III)V");
		if(data->javaVideoGrabber && javaInitGrabber){
			env->CallVoidMethod(data->javaVideoGrabber,javaInitGrabber,w,h,data->attemptFramerate);
		}else{
			ofLogError("ofxAndroidVideoGrabber") << "initGrabber(): couldn't get OFAndroidVideoGrabber init grabber method";
			return false;
		}
	}

	//ofLogVerbose("ofxAndroidVideoGrabber") << "initGrabber(): new frame callback size: " << (int) width << "x" << (int) height;
	data->frontPixels.allocate(w,h,getPixelFormat());
	data->bGrabberInited = true;

	ofLogVerbose("ofxAndroidVideoGrabber") << "initGrabber(): camera initialized correctly";
	data->appPaused = false;
	return true;
}

bool ofxAndroidVideoGrabber::isInitialized() const{
	return data->bGrabberInited;
}

void ofxAndroidVideoGrabber::videoSettings(){
}

ofPixels&	ofxAndroidVideoGrabber::getPixels(){
	return data->frontPixels;
}

const ofPixels& ofxAndroidVideoGrabber::getPixels() const {
    return data->frontPixels;
}

void ofxAndroidVideoGrabber::setVerbose(bool bTalkToMe){

}

void ofxAndroidVideoGrabber::setDeviceID(int _deviceID){

	JNIEnv *env = ofGetJNIEnv();
	if(!env) return;

	jclass javaClass = getJavaClass();

	jmethodID javasetDeviceID = env->GetMethodID(javaClass,"setDeviceID","(I)V");
	if(data->javaVideoGrabber && javasetDeviceID){
		env->CallVoidMethod(data->javaVideoGrabber,javasetDeviceID,_deviceID);
	}else{
		ofLogError("ofxAndroidVideoGrabber") << "setDeviceID(): couldn't get OFAndroidVideoGrabber setDeviceID method";
		return;
	}
}

bool ofxAndroidVideoGrabber::setAutoFocus(bool autofocus){
	JNIEnv *env = ofGetJNIEnv();
	if(!env) return false;

	jclass javaClass = getJavaClass();
	jmethodID javasetAutoFocus = env->GetMethodID(javaClass,"setAutoFocus","(Z)Z");
	if(data->javaVideoGrabber && javasetAutoFocus){
		return env->CallBooleanMethod(data->javaVideoGrabber,javasetAutoFocus,autofocus);
	}else{
		ofLogError("ofxAndroidVideoGrabber") << "setAutoFocus(): couldn't get OFAndroidVideoGrabber setAutoFocus method";
		return false;
	}
}

void ofxAndroidVideoGrabber::setDesiredFrameRate(int framerate){
	data->attemptFramerate = framerate;
}

float ofxAndroidVideoGrabber::getHeight() const{
	return data->frontPixels.getHeight();
}

float ofxAndroidVideoGrabber::getWidth() const{
	return data->frontPixels.getWidth();
}

bool ofxAndroidVideoGrabber::setPixelFormat(ofPixelFormat pixelFormat){
	data->internalPixelFormat = pixelFormat;
	return true;
}

ofPixelFormat ofxAndroidVideoGrabber::getPixelFormat() const{
	return data->internalPixelFormat;
}

// Conversion from yuv nv21 to rgb24 adapted from
// videonet conversion from YUV420 to RGB24
// http://www.codeguru.com/cpp/g-m/multimedia/video/article.php/c7621
 static long int crv_tab[256];
 static long int cbu_tab[256];
 static long int cgu_tab[256];
 static long int cgv_tab[256];
 static long int tab_76309[256];
 static unsigned char clp[1024];         //for clip in CCIR601
 //
 //Initialize conversion table for YUV420 to RGB
 //
 void InitConvertTable()
 {
	static bool inited = false;
	if(inited) return;
    long int crv,cbu,cgu,cgv;
    int i,ind;

    crv = 104597; cbu = 132201;  /* fra matrise i global.h */
    cgu = 25675;  cgv = 53279;

    for (i = 0; i < 256; i++) {
       crv_tab[i] = (i-128) * crv;
       cbu_tab[i] = (i-128) * cbu;
       cgu_tab[i] = (i-128) * cgu;
       cgv_tab[i] = (i-128) * cgv;
       tab_76309[i] = 76309*(i-16);
    }

    for (i=0; i<384; i++)
       clp[i] =0;
    ind=384;
    for (i=0;i<256; i++)
        clp[ind++]=i;
    ind=640;
    for (i=0;i<384;i++)
        clp[ind++]=255;

    inited = true;
 }

 void ConvertYUV2RGB(unsigned char *src0,unsigned char *src1,unsigned char *dst_ori,
                                  int width,int height)
 {
     register int y1,y2,u,v;
     register unsigned char *py1,*py2;
     register int i,j, c1, c2, c3, c4;
     register unsigned char *d1, *d2;

     int width3 = 3*width;
     py1=src0;
     py2=py1+width;
     d1=dst_ori;
     d2=d1+width3;
     for (j = 0; j < height; j += 2) {
         for (i = 0; i < width; i += 2) {

             v = *src1++;
             u = *src1++;

             c1 = crv_tab[v];
             c2 = cgu_tab[u];
             c3 = cgv_tab[v];
             c4 = cbu_tab[u];

             //up-left
             y1 = tab_76309[*py1++];
             *d1++ = clp[384+((y1 + c1)>>16)];
             *d1++ = clp[384+((y1 - c2 - c3)>>16)];
             *d1++ = clp[384+((y1 + c4)>>16)];

             //down-left
             y2 = tab_76309[*py2++];
             *d2++ = clp[384+((y2 + c1)>>16)];
             *d2++ = clp[384+((y2 - c2 - c3)>>16)];
             *d2++ = clp[384+((y2 + c4)>>16)];

             //up-right
             y1 = tab_76309[*py1++];
             *d1++ = clp[384+((y1 + c1)>>16)];
             *d1++ = clp[384+((y1 - c2 - c3)>>16)];
             *d1++ = clp[384+((y1 + c4)>>16)];

             //down-right
             y2 = tab_76309[*py2++];
             *d2++ = clp[384+((y2 + c1)>>16)];
             *d2++ = clp[384+((y2 - c2 - c3)>>16)];
             *d2++ = clp[384+((y2 + c4)>>16)];
         }
         d1 += width3;
         d2 += width3;
         py1+=   width;
         py2+=   width;
     }


}
/**
 * Converts semi-planar YUV420 as generated for camera preview into RGB565
 * format for use as an OpenGL ES texture. It assumes that both the input
 * and output data are contiguous and start at zero.
 *
 * @param yuvs the array of YUV420 semi-planar data
 * @param rgbs an array into which the RGB565 data will be written
 * @param width the number of pixels horizontally
 * @param height the number of pixels vertically
 */

//we tackle the conversion two pixels at a time for greater speed
void ConvertYUV2toRGB565(unsigned char* yuvs, unsigned char* rgbs, int width, int height) {
    //the end of the luminance data
    int lumEnd = width * height;
    //points to the next luminance value pair
    int lumPtr = 0;
    //points to the next chromiance value pair
    int chrPtr = lumEnd;
    //points to the next byte output pair of RGB565 value
    int outPtr = 0;
    //the end of the current luminance scanline
    int lineEnd = width;
    register int R, G, B;
    register int Y1;
    register int Y2;
    register int Cr;
    register int Cb;

    while (true) {

        //skip back to the start of the chromiance values when necessary
        if (lumPtr == lineEnd) {
            if (lumPtr == lumEnd) break; //we've reached the end
            //division here is a bit expensive, but's only done once per scanline
            chrPtr = lumEnd + ((lumPtr  >> 1) / width) * width;
            lineEnd += width;
        }

        //read the luminance and chromiance values
        Y1 = yuvs[lumPtr++] & 0xff;
        Y2 = yuvs[lumPtr++] & 0xff;
        Cr = (yuvs[chrPtr++] & 0xff) - 128;
        Cb = (yuvs[chrPtr++] & 0xff) - 128;

        //generate first RGB components
        B = Y1 + ((454 * Cb) >> 8);
        if(B < 0) B = 0; else if(B > 255) B = 255;
        G = Y1 - ((88 * Cb + 183 * Cr) >> 8);
        if(G < 0) G = 0; else if(G > 255) G = 255;
        R = Y1 + ((359 * Cr) >> 8);
        if(R < 0) R = 0; else if(R > 255) R = 255;
        //NOTE: this assume little-endian encoding
        rgbs[outPtr++]  = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
        rgbs[outPtr++]  = (unsigned char) ((R & 0xf8) | (G >> 5));

        //generate second RGB components
        B = Y2 + ((454 * Cb) >> 8);
        if(B < 0) B = 0; else if(B > 255) B = 255;
        G = Y2 - ((88 * Cb + 183 * Cr) >> 8);
        if(G < 0) G = 0; else if(G > 255) G = 255;
        R = Y2 + ((359 * Cr) >> 8);
        if(R < 0) R = 0; else if(R > 255) R = 255;
        //NOTE: this assume little-endian encoding
        rgbs[outPtr++]  = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
        rgbs[outPtr++]  = (unsigned char) ((R & 0xf8) | (G >> 5));
    }
}
void ConvertYUV2toRGB565_2(unsigned char *src0,unsigned char *src1,unsigned char *dst_ori,
                                 int width,int height)
{
    register int y1,y2,u,v;
    register unsigned char *py1,*py2;
    register int i,j, c1, c2, c3, c4;
    register unsigned char *d1, *d2;
    register int R,G,B;
    int width2 = 2*width;
    py1=src0;
    py2=py1+width;
    d1=dst_ori;
    d2=d1+width2;
    for (j = 0; j < height; j += 2) {
        for (i = 0; i < width; i += 2) {

            v = *src1++;
            u = *src1++;

            c1 = crv_tab[v];
            c2 = cgu_tab[u];
            c3 = cgv_tab[v];
            c4 = cbu_tab[u];

            //up-left
            y1 = tab_76309[*py1++];
            R = clp[384+((y1 + c1)>>16)];
            G = clp[384+((y1 - c2 - c3)>>16)];
            B = clp[384+((y1 + c4)>>16)];
            *d1++ = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
            *d1++ = (unsigned char) ((R & 0xf8) | (G >> 5));

            //down-left
            y2 = tab_76309[*py2++];
            B = clp[384+((y2 + c1)>>16)];
            G = clp[384+((y2 - c2 - c3)>>16)];
            R = clp[384+((y2 + c4)>>16)];
            *d2++ = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
            *d2++ = (unsigned char) ((R & 0xf8) | (G >> 5));

            //up-right
            y1 = tab_76309[*py1++];
            R = clp[384+((y1 + c1)>>16)];
            G = clp[384+((y1 - c2 - c3)>>16)];
            B = clp[384+((y1 + c4)>>16)];
            *d1++ = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
            *d1++ = (unsigned char) ((R & 0xf8) | (G >> 5));

            //down-right
            y2 = tab_76309[*py2++];
            B = clp[384+((y2 + c1)>>16)];
            G = clp[384+((y2 - c2 - c3)>>16)];
            R = clp[384+((y2 + c4)>>16)];
            *d2++ = (unsigned char) (((G & 0x3c) << 3) | (B >> 3));
            *d2++ = (unsigned char) ((R & 0xf8) | (G >> 5));
        }
        d1 += width2;
        d2 += width2;
        py1+=   width;
        py2+=   width;
    }


}

 /*static int time_one_frame = 0;
 static int acc_time = 0;
 static int num_frames = 0;
 static int time_prev_out = 0;*/

extern "C"{
jint
Java_cc_openframeworks_OFAndroidVideoGrabber_newFrame(JNIEnv*  env, jobject  thiz, jbyteArray array, jint width, jint height, jint cameraId){
	auto data = instances()[cameraId].lock();
	if(!data) return 1;

	auto currentFrame = (unsigned char*)env->GetPrimitiveArrayCritical(array, NULL);
	if(!currentFrame) return 1;

	ofPixels & pixels = data->backPixels;
	bool needsResize=false;
	if(pixels.getWidth()!=width || pixels.getHeight()!=height){
		needsResize = true;
		pixels.allocate(width,height,data->internalPixelFormat);
	}



	//time_one_frame = ofGetSystemTime();
	if(data->internalPixelFormat==OF_PIXELS_RGB){
		ConvertYUV2RGB(currentFrame, 					// y component
				currentFrame+(width*height),			// uv components
				pixels.getData(),width,height);
	}else if(data->internalPixelFormat==OF_PIXELS_RGB565){
		ConvertYUV2toRGB565(currentFrame,pixels.getData(),width,height);
	}else if(data->internalPixelFormat==OF_PIXELS_MONO){
		pixels.setFromPixels(currentFrame,width,height,OF_IMAGE_GRAYSCALE);
	}

	if(needsResize){
		data->backPixels.resize(data->frontPixels.getWidth(), data->frontPixels.getHeight(), OF_INTERPOLATE_NEAREST_NEIGHBOR);
	}
	/*acc_time += ofGetSystemTime() - time_one_frame;
	num_frames ++;
	if(ofGetSystemTime() - time_prev_out > 5000){
		time_prev_out = ofGetSystemTime();
		ofLogNotice("ofxAndroidVideoGrabber") << "avg time: " << float(acc_time)/float(num_frames);
	}*/

	env->ReleasePrimitiveArrayCritical(array,currentFrame ,0);
	data->newPixels = true;
	return 0;
}
}
