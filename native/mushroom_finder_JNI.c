/*
 * Giant Mushroom Island Finder — JNI bridge (C)
 *
 * Architecture inspired by SunnySlopes FortressFinderGUI / RiverFinderGUI
 * (Java GUI + JNI + native search). No source code copied.
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "mushroom_finder.h"

/* Flat result layout per island (15 doubles):
 * 0:centerX 1:centerZ 2:minX 3:maxX 4:minZ 5:maxZ
 * 6:width 7:height 8:area 9:samples 10:distance 11:compactness 12:perimeter
 * 13:diagonal_span 14:area_precision
 */
#define RESULT_STRIDE 15

JNIEXPORT jdoubleArray JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeSearch(
    JNIEnv *env, jclass cls,
    jlong seed, jstring version, jint radius, jlong minArea,
    jint scale, jboolean refine, jboolean includeShore,
    jlong controlPtr, jint threads, jint maxResults)
{
    (void)cls;
    const char *vchars = (*env)->GetStringUTFChars(env, version, NULL);
    if (!vchars) return NULL;
    int mc = gmif_mc_from_string(vchars);
    (*env)->ReleaseStringUTFChars(env, version, vchars);
    if (mc < 0) return NULL;

    SearchParams p;
    memset(&p, 0, sizeof(p));
    p.seed = (int64_t)seed;
    p.mc = mc;
    p.radius = (int)radius;
    p.min_area = (int64_t)minArea;
    p.scale = (int)scale;
    p.refine = refine ? 1 : 0;
    p.include_shore = includeShore ? 1 : 0;
    p.threads = threads > 0 ? (int)threads : 1;
    if (p.threads > 256) p.threads = 256;
    p.max_results = maxResults > 0 ? (int)maxResults : 0;
    if (p.radius > 30000000) p.radius = 30000000;
    if (p.radius < 1) p.radius = 1;
    p.use_tiles = 2; /* auto: tiled when grid would not fit */
    p.tile_size = 0;

    GmifControl *ctrl = (GmifControl *)(intptr_t)controlPtr;

    MushroomIsland *islands = NULL;
    int count = 0;
    int rc = gmif_search(&p, &islands, &count, NULL, NULL, ctrl, NULL);

    if (rc != 0) {
        free(islands);
        /* Error message stays in gmif_last_error() for Java to fetch. */
        return NULL;
    }

    jsize n = (jsize)count * RESULT_STRIDE;
    jdoubleArray arr = (*env)->NewDoubleArray(env, n < 0 ? 0 : n);
    if (!arr) {
        free(islands);
        return NULL;
    }
    if (count > 0) {
        jdouble *tmp = (jdouble *)malloc(sizeof(jdouble) * (size_t)n);
        if (!tmp) {
            free(islands);
            return arr;
        }
        for (int i = 0; i < count; i++) {
            const MushroomIsland *isl = &islands[i];
            jdouble *o = tmp + (size_t)i * RESULT_STRIDE;
            o[0] = isl->center_x;
            o[1] = isl->center_z;
            o[2] = isl->min_x;
            o[3] = isl->max_x;
            o[4] = isl->min_z;
            o[5] = isl->max_z;
            o[6] = isl->width;
            o[7] = isl->height;
            o[8] = (jdouble)isl->area;
            o[9] = (jdouble)isl->samples;
            o[10] = isl->distance;
            o[11] = isl->compactness;
            o[12] = isl->perimeter;
            o[13] = isl->diagonal_span;
            o[14] = isl->area_precision;
        }
        (*env)->SetDoubleArrayRegion(env, arr, 0, n, tmp);
        free(tmp);
    }
    free(islands);
    return arr;
}

JNIEXPORT void JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativePollProgress(
    JNIEnv *env, jclass cls, jlong ptr, jintArray out3)
{
    (void)cls;
    GmifControl *c = (GmifControl *)(intptr_t)ptr;
    if (!c || !out3) return;
    jint tmp[3];
    tmp[0] = c->percent;
    tmp[1] = (jint)(c->found > 0x7fffffff ? 0x7fffffff : c->found);
    tmp[2] = c->stage;
    (*env)->SetIntArrayRegion(env, out3, 0, 3, tmp);
}

JNIEXPORT jstring JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeVersionString(
    JNIEnv *env, jclass cls, jint mc)
{
    (void)cls;
    const char *s = gmif_mc_to_string((int)mc);
    return (*env)->NewStringUTF(env, s ? s : "?");
}

JNIEXPORT jint JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeVersionId(
    JNIEnv *env, jclass cls, jstring s)
{
    (void)cls;
    const char *c = (*env)->GetStringUTFChars(env, s, NULL);
    if (!c) return -1;
    int mc = gmif_mc_from_string(c);
    (*env)->ReleaseStringUTFChars(env, s, c);
    return mc;
}

JNIEXPORT jobjectArray JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeKnownVersions(
    JNIEnv *env, jclass cls)
{
    (void)cls;
    const char *const *v = gmif_known_versions();
    int n = 0;
    while (v[n]) n++;
    jclass strCls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray arr = (*env)->NewObjectArray(env, n, strCls, NULL);
    for (int i = 0; i < n; i++) {
        jstring s = (*env)->NewStringUTF(env, v[i]);
        (*env)->SetObjectArrayElement(env, arr, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    return arr;
}

JNIEXPORT jlong JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeCreateControl(
    JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    GmifControl *c = (GmifControl *)calloc(1, sizeof(GmifControl));
    if (c) gmif_control_init(c);
    return (jlong)(intptr_t)c;
}

JNIEXPORT void JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativePause(
    JNIEnv *env, jclass cls, jlong ptr)
{
    (void)env; (void)cls;
    gmif_control_pause((GmifControl *)(intptr_t)ptr);
}

JNIEXPORT void JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeResume(
    JNIEnv *env, jclass cls, jlong ptr)
{
    (void)env; (void)cls;
    gmif_control_resume((GmifControl *)(intptr_t)ptr);
}

JNIEXPORT void JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeStop(
    JNIEnv *env, jclass cls, jlong ptr)
{
    (void)env; (void)cls;
    gmif_control_stop((GmifControl *)(intptr_t)ptr);
}

JNIEXPORT void JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeDestroyControl(
    JNIEnv *env, jclass cls, jlong ptr)
{
    (void)env; (void)cls;
    free((void *)(intptr_t)ptr);
}

JNIEXPORT jstring JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeGetLastError(
    JNIEnv *env, jclass cls)
{
    (void)cls;
    const char *msg = gmif_last_error();
    return (*env)->NewStringUTF(env, msg ? msg : "");
}

JNIEXPORT jint JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeGetLastErrorCode(
    JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    return gmif_last_error_code();
}

/*
 * describeGrid(radius, scale) -> long[5]
 * [0]=half [1]=sx [2]=sz [3]=cells [4]=biome_bytes
 * On failure returns null; message in last error.
 */
JNIEXPORT jlongArray JNICALL
Java_dev_sakuhime_mushroomfinder_NativeBridge_nativeDescribeGrid(
    JNIEnv *env, jclass cls, jint radius, jint scale)
{
    (void)cls;
    int half = 0, sx = 0, sz = 0;
    int64_t cells = 0, mask_b = 0, biome_b = 0;
    int rc = gmif_describe_grid((int)radius, (int)scale,
                                &half, &sx, &sz, &cells, &mask_b, &biome_b);
    if (rc != 0) return NULL;
    jlong tmp[5];
    tmp[0] = half;
    tmp[1] = sx;
    tmp[2] = sz;
    tmp[3] = cells;
    tmp[4] = biome_b;
    jlongArray arr = (*env)->NewLongArray(env, 5);
    if (arr) (*env)->SetLongArrayRegion(env, arr, 0, 5, tmp);
    return arr;
}
