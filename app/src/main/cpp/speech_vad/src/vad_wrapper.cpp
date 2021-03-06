//
// Created by yangd on 2018/5/16.
//

#include <mutex>

#include "simple_vad.h"
#include "file_cut.h"
#include "period_format.h"
#include <jni.h>
#include <queue>
#include "stream_thread.h"
#include "stream_bean.h"
#include "frame_info.h"

static simple_vad *vad = NULL;
uint64_t currentPos=0;
int add_period_activity(struct periods *per, int is_active, int is_last) {
    static int old_is_active = 0;
    static int count = 0;
    int res_add = period_add_vad_activity(per, is_active, is_last);
    if (res_add != 0) {
        return res_add;
    }
    if (is_active != old_is_active) {
        // printf("%s,%d \n", old_is_active ? "A" : "I", count);
        // I,1  表示之前的1个FRAME是 INACTIVE的；
        // I,1 A,10 表示之前的1个FRAME是 INACTIVE的；第2-10个FRAME是ACTIVE的
        // periods_print(per);
        old_is_active = is_active;
    }
    count += 1;
    if (is_last) {
        periods_print(per);
        printf("total frames %d\n", count);
    }
    return 0;
}

Frame runWithStream(const StreamBean &bean, simple_vad *vad) {
    int is_active = process_vad(vad, bean.data);//送入到webrtc中进行识别vad,true说明有声音
    Frame frame;
    frame.start=currentPos;
    currentPos+=bean.length*2;
    frame.end=currentPos-1;
    frame.isActive= static_cast<bool>(is_active);
    return frame;
}

int run(FILE *fp, simple_vad *vad, struct cut_info *cut) {

    int16_t data[FRAME_SIZE];
    int res = 0;
    struct periods *per = periods_create();
    int kk = 0;
    while (res == 0) {
        res = read_int16_bytes(fp, data);
        if (res <= 1) {
            int is_last = (res == 1);//是否是最后一片Frame
            int is_active = process_vad(vad, data);
            if (is_active == 1) {
                ALOGE("is active=%d", is_active);
            }
            add_period_activity(per, is_active, is_last);
            int vad_file_res = cut_add_vad_activity(cut, is_active, is_last);
            if (vad_file_res < 0) {
                printf("file write success %s\n", cut->result_filename);
            }
        } else if (ferror(fp)) {
            printf("read failed  ferror result  : %d\n", ferror(fp));
        }

    }
    periods_free(per);

    if (res != 1) {
        fprintf(stderr, "read file error %d\n", res);
        return res;
    }
    return 0;
}

bool Init() {
    if (vad != NULL) return true;
    vad = simple_vad_create();
    if (vad == NULL) {
        return false;
    }
    return true;
}

bool RunVADWithFile(int16_t *data, const std::string &input_file, const std::string &output_dir,
                    const std::string &output_filename_prefix) {
    if (data == nullptr) {//区分传进来的是文件还是流
        FILE *fp = fopen(input_file.c_str(), "rb");
        if (fp == NULL) return false;
        FILE *fp2 = fopen(input_file.c_str(), "rb");
        if (fp2 == NULL) return false;
        struct cut_info *cut = cut_info_create(fp2);
        snprintf(cut->output_filename_prefix, sizeof(cut->output_filename_prefix), "%s",
                 output_filename_prefix.c_str());
        snprintf(cut->output_file_dir, sizeof(cut->output_file_dir), "%s",
                 output_dir.c_str());
        int res = run(fp, vad, cut);
        fclose(fp);
        fclose(fp2);
        cut_info_free(cut);
        printf("PROGRAM FINISH\n");
        return res == 0;
    }
    return 0;
}


extern "C" JNIEXPORT jboolean JNICALL
Java_com_sogou_translate_vad_JSimpleVad_CVadInit(JNIEnv *env,
                                                 jobject thiz) {
    return Init();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sogou_translate_vad_JSimpleVad_CDoVad(JNIEnv *env,
                                               jobject thiz,
                                               jstring input_file,
                                               jstring output_path,
                                               jstring output_prefix) {
    jboolean iscopy = true;
    const char *pinput = env->GetStringUTFChars(input_file, &iscopy);
    const char *poutpath = env->GetStringUTFChars(output_path, &iscopy);
    const char *poutprefix = env->GetStringUTFChars(output_prefix, &iscopy);
    std::string str_input_file = pinput;
    std::string str_output_path = poutpath;
    std::string str_output_prefix = poutprefix;
    env->ReleaseStringUTFChars(input_file, pinput);
    env->ReleaseStringUTFChars(output_path, poutpath);
    env->ReleaseStringUTFChars(output_prefix, poutprefix);
    return RunVADWithFile(nullptr, str_input_file, str_output_path, str_output_prefix);
}



extern "C" JNIEXPORT void JNICALL
Java_com_sogou_translate_vad_JSimpleVad_doVad(JNIEnv *env,
                                              jobject thiz,
                                              jshortArray inputData) {
    jboolean iscopy = true;
    //转为short
    int16_t *pinput = env->GetShortArrayElements(inputData, &iscopy);
    jsize length = env->GetArrayLength(inputData);
    StreamBean bean;//添加到池中
    bean.data = pinput;
    bean.length = length;
    env->ReleaseShortArrayElements(inputData, pinput, 0);
}
extern "C" JNIEXPORT void JNICALL
Java_com_sogou_translate_vad_JSimpleVad_releaseMemory(JNIEnv *env,
                                                      jobject thiz) {
//    clear(stream_queue);
    if (vad != nullptr) {
        simple_vad_free(vad);
    }

}
extern "C" JNIEXPORT jobject JNICALL
Java_com_sogou_translate_vad_JSimpleVad_vadStream(JNIEnv *env,
                                                   jobject thiz, jshortArray inputData,
                                                   jstring clazzPath) {
    jboolean iscopy = true;
    //转为short
    jshort *pinput = (env->GetShortArrayElements(inputData, &iscopy));
    jsize length = env->GetArrayLength(inputData);
    const char *path = env->GetStringUTFChars(clazzPath, &iscopy);
    StreamBean bean;//添加到池中
    bean.data = pinput;
    bean.length = length;
    env->ReleaseShortArrayElements(inputData, pinput, 0);
    Frame frame=runWithStream(bean, vad);
    //反射传递
    jclass clazz = env->FindClass(path);
    if (clazz == NULL) {
        return NULL;
    }
    jmethodID constructorMid = env->GetMethodID(clazz, "<init>", "(JZJ)V");
    jobject jobject1=env->NewObject(clazz,constructorMid,frame.start, frame.isActive,frame.end);
    return jobject1;

}
extern "C"
JNIEXPORT void JNICALL
Java_com_sogou_translate_vad_JSimpleVad_restore(JNIEnv *env, jclass type) {


}