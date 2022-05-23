#include "json.hpp"
#include "gzip/decompress.hpp"

#include <VapourSynth4.h>
#include <VSHelper4.h>

#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <utility>
#include <span>
#include <algorithm>

#include <iostream>
#include <fstream>

using nlohmann::json;

struct IvtcData {
    VSNode* videoNode;
    VSVideoInfo vi;
    std::vector<std::pair<uint_fast32_t, uint_fast32_t>> fieldsForFrames;
    std::map<uint_fast32_t, std::string> freezeFrameHandling;
    VSNode* linedoubledNode;

    IvtcData() : videoNode(nullptr), fieldsForFrames(), linedoubledNode(nullptr) {}
};

static std::pair<uint_fast32_t, uint_fast32_t> EMPTY_PAIR = std::make_pair(UINT_FAST32_MAX, UINT_FAST32_MAX);

static const VSFrame *VS_CC ivtcGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    IvtcData* d = static_cast<IvtcData*>(instanceData);

    auto requiredFields = d->fieldsForFrames.at(n);
    if (requiredFields == EMPTY_PAIR) {
        return nullptr;
    }

    if (activationReason == arInitial) {
        if (requiredFields.first != UINT_FAST32_MAX && requiredFields.second != UINT_FAST32_MAX) {
            // We will resolve by field matching
            vsapi->requestFrameFilter(requiredFields.first, d->videoNode, frameCtx);
            vsapi->requestFrameFilter(requiredFields.second, d->videoNode, frameCtx);
            return nullptr;
        }

        // One field is missing, use linedoubled clip if available, otherwise we'll do it naively
        VSNode* videoNode = d->linedoubledNode == nullptr ? d->videoNode : d->linedoubledNode;
        if (requiredFields.first != UINT_FAST32_MAX) {
            vsapi->requestFrameFilter(requiredFields.first, videoNode, frameCtx);
        } else {
            vsapi->requestFrameFilter(requiredFields.second, videoNode, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        auto top = requiredFields.first;
        auto bottom = requiredFields.second;

        VSFrame* dst;
        VSMap* props;
        if (d->linedoubledNode == nullptr || (top != UINT_FAST32_MAX && bottom != UINT_FAST32_MAX)) {
            if (top == UINT_FAST32_MAX) {
                top = bottom;
            }
            if (bottom == UINT_FAST32_MAX) {
                bottom = top;
            }
            const VSFrame* topFrame = vsapi->getFrameFilter(top, d->videoNode, frameCtx);
            const VSFrame* bottomFrame = vsapi->getFrameFilter(bottom, d->videoNode, frameCtx);

            dst = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, topFrame, core);
            const VSVideoFormat* fi = vsapi->getVideoFrameFormat(dst);

            for (int plane = 0; plane < fi->numPlanes; plane++) {
                const uint8_t* srcpTop = vsapi->getReadPtr(topFrame, plane);
                const uint8_t* srcpBottom = vsapi->getReadPtr(bottomFrame, plane);
                ptrdiff_t srcStride = vsapi->getStride(bottomFrame, plane);
                uint8_t* dstp = vsapi->getWritePtr(dst, plane);
                ptrdiff_t dstStride = vsapi->getStride(dst, plane);
                int h = vsapi->getFrameHeight(topFrame, plane);
                size_t rowSize = vsapi->getFrameWidth(dst, plane) * fi->bytesPerSample;

                for (int hl = 0; hl < h; hl++) {
                    memcpy(dstp, srcpTop, rowSize);
                    dstp += dstStride;
                    memcpy(dstp, srcpBottom, rowSize);
                    srcpBottom += srcStride;
                    srcpTop += srcStride;
                    dstp += dstStride;
                }
            }
            props = vsapi->getFramePropertiesRW(dst);
            vsapi->mapSetInt(props, "IVTCDN_Fields", top == bottom ? 1 : 2, maReplace);

            vsapi->freeFrame(topFrame);
            vsapi->freeFrame(bottomFrame);
        } else {
            auto frameNumber = top == UINT_FAST32_MAX ? bottom : top;
            const VSFrame* src = vsapi->getFrameFilter(frameNumber, d->linedoubledNode, frameCtx);
            dst = vsapi->copyFrame(src, core);
            props = vsapi->getFramePropertiesRW(dst);
            vsapi->mapSetInt(props, "IVTCDN_Fields", 1, maReplace);

            vsapi->freeFrame(src);
        }
        if (d->freezeFrameHandling.contains(n)) {
            auto freezeFrame = d->freezeFrameHandling[n];
            vsapi->mapSetData(props, "IVTCDN_FreezeFrame", freezeFrame.c_str(), freezeFrame.size(), dtUtf8, maReplace);
        }
        vsapi->mapDeleteKey(props, "_Field");
        vsapi->mapSetInt(props, "_FieldBased", 0, maReplace);
        return dst;
    }

    return nullptr;
}

static void VS_CC ivtcFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    IvtcData *d = static_cast<IvtcData *>(instanceData);
    vsapi->freeNode(d->videoNode);
    delete d;
}

static int_fast8_t TOP_FRAMES[] = {0, 2, 4, 6};
static int_fast8_t BOTTOM_FRAMES[] = {1, 3, 5, 7};
static int_fast8_t COMPLETE_PREVIOUS_CYCLE = 9;

static void VS_CC ivtcCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<IvtcData> d(new IvtcData());
    int err = 0;

    d->videoNode = vsapi->mapGetNode(in, "clip", 0, nullptr);
    d->linedoubledNode = vsapi->mapGetNode(in, "linedoubled", 0, &err); // TODO sanity checks
    if (err) {
        d->linedoubledNode = nullptr;
    }
    d->vi = *vsapi->getVideoInfo(d->videoNode);

    std::string projectFilename = vsapi->mapGetData(in, "projectfile", 0, nullptr);
    bool rawProject = !!vsapi->mapGetInt(in, "rawproject", 0, &err);

    std::string decompressed;
    if (rawProject) {
        decompressed = projectFilename;
    } else {
        std::ifstream input(projectFilename, std::ios::binary | std::ios::ate);
        int inputSize = input.tellg();
        input.seekg(0, std::ios::beg);

        std::vector<char> compressed;
        compressed.resize(inputSize);
        input.read(compressed.data(), compressed.size());
        decompressed = gzip::decompress(compressed.data(), compressed.size());
    }

    json projectData = json::parse(decompressed);

    std::vector<std::int_fast8_t> actions = projectData["ivtc_actions"];

    size_t expectedFields = actions.size();
    size_t completeCycles = expectedFields / 10;
    uint_fast8_t fieldsWithIncompleteCycles = expectedFields % 10;
    size_t totalCycles = fieldsWithIncompleteCycles == 0 ? completeCycles : completeCycles + 1;
    size_t outputFrameCount = completeCycles * 4 + fieldsWithIncompleteCycles * 2 / 5;

    std::pair<uint_fast32_t, uint_fast32_t> lastSpecifiedFrame = EMPTY_PAIR;
    uint_fast32_t pushCount = 1;
    for (auto cycleIdx = 0; cycleIdx < totalCycles; cycleIdx++) {
        uint_fast8_t actionCountInCycle = cycleIdx == completeCycles ? fieldsWithIncompleteCycles : 10;
        auto actionsIdx = cycleIdx * 10;
        std::span<std::int_fast8_t> cycleActions(actions.begin() + actionsIdx, actionCountInCycle);
        uint_fast8_t frameCountInCycle = actionCountInCycle * 2 / 5;
        for (auto frameIdx = 0; frameIdx < frameCountInCycle; frameIdx++) {
            uint_fast32_t activeFrame = cycleIdx * 4 + frameIdx;
            int_fast8_t top;
            int_fast8_t bottom;
            auto it = std::find(cycleActions.begin(), cycleActions.end(), TOP_FRAMES[frameIdx]);
            top = it == cycleActions.end() ? -1 : it - cycleActions.begin();
            it = std::find(cycleActions.begin(), cycleActions.end(), BOTTOM_FRAMES[frameIdx]);
            bottom = it == cycleActions.end() ? -1 : it - cycleActions.begin();

            if (top == -1 && bottom == -1) {
                std::string activeFrameJsonKey = std::to_string(activeFrame);
                if (frameIdx == 3 && (actionsIdx + 10) < actions.size() && actions[actionsIdx + 10] == COMPLETE_PREVIOUS_CYCLE) {
                    lastSpecifiedFrame = std::make_pair(actionsIdx + 10, UINT_FAST32_MAX);
                } else if (projectData["no_match_handling"].contains(activeFrameJsonKey) && projectData["no_match_handling"][activeFrameJsonKey] == "Next") {
                    d->freezeFrameHandling[activeFrame] = "Next";
                    lastSpecifiedFrame = EMPTY_PAIR;
                } else {
                    d->freezeFrameHandling[activeFrame] = "Previous";
                }
            } else if (top != -1) {
                if (bottom != -1) {
                    lastSpecifiedFrame = std::make_pair(actionsIdx + top, actionsIdx + bottom);
                } else {
                    lastSpecifiedFrame = std::make_pair(actionsIdx + top, UINT_FAST32_MAX);
                }
            } else { // bottom != -1
                if (frameIdx == 3 && (actionsIdx + 10) < actions.size() && actions[actionsIdx + 10] == COMPLETE_PREVIOUS_CYCLE) {
                    lastSpecifiedFrame = std::make_pair(actionsIdx + 10, actionsIdx + bottom);
                } else {
                    lastSpecifiedFrame = std::make_pair(UINT_FAST32_MAX, actionsIdx + bottom);
                }
            }

            if (lastSpecifiedFrame == EMPTY_PAIR) {
                pushCount++;
            } else {
                for (auto i = 0; i < pushCount; i++) {
                    d->fieldsForFrames.push_back(lastSpecifiedFrame);
                }
                pushCount = 1;
            }
        }
    }

    d->vi.numFrames = outputFrameCount;
    d->vi.height *= 2;
    d->vi.fpsNum *= 2;
    d->vi.fpsNum /= 5;

    VSFilterDependency deps[] = {{ d->videoNode, rpGeneral }};
    vsapi->createVideoFilter(out, "IVTC", &d->vi, ivtcGetFrame, ivtcFree, fmParallel, deps, 1, d.get(), core);
    d.release();
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("tools.mike.ivtc", "ivtcdn", "Apply IVTC DN project file to clip", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("IVTC", "clip:vnode;projectfile:data;rawproject:int:opt;linedoubled:vnode:opt;", "clip:vnode;", ivtcCreate, nullptr, plugin);
}