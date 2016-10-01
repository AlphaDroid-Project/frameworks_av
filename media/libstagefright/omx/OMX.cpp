/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>

//#define LOG_NDEBUG 0
#define LOG_TAG "OMX"
#include <utils/Log.h>

#include <dlfcn.h>

#include "../include/OMX.h"

#include "../include/OMXNodeInstance.h"

#include <media/stagefright/foundation/ADebug.h>

#include "OMXMaster.h"
#include "OMXUtils.h"

namespace android {

// node ids are created by concatenating the pid with a 16-bit counter
static size_t kMaxNodeInstances = (1 << 16);

OMX::OMX()
    : mMaster(new OMXMaster) {
}

OMX::~OMX() {
    delete mMaster;
    mMaster = NULL;
}

void OMX::binderDied(const wp<IBinder> &the_late_who) {
    sp<OMXNodeInstance> instance;

    {
        Mutex::Autolock autoLock(mLock);

        ssize_t index = mLiveNodes.indexOfKey(the_late_who);

        if (index < 0) {
            ALOGE("b/27597103, nonexistent observer on binderDied");
            android_errorWriteLog(0x534e4554, "27597103");
            return;
        }

        instance = mLiveNodes.editValueAt(index);
        mLiveNodes.removeItemsAt(index);
    }

    instance->onObserverDied();
}

status_t OMX::listNodes(List<ComponentInfo> *list) {
    list->clear();

    OMX_U32 index = 0;
    char componentName[256];
    while (mMaster->enumerateComponents(
                componentName, sizeof(componentName), index) == OMX_ErrorNone) {
        list->push_back(ComponentInfo());
        ComponentInfo &info = *--list->end();

        info.mName = componentName;

        Vector<String8> roles;
        OMX_ERRORTYPE err =
            mMaster->getRolesOfComponent(componentName, &roles);

        if (err == OMX_ErrorNone) {
            for (OMX_U32 i = 0; i < roles.size(); ++i) {
                info.mRoles.push_back(roles[i]);
            }
        }

        ++index;
    }

    return OK;
}

status_t OMX::allocateNode(
        const char *name, const sp<IOMXObserver> &observer,
        sp<IBinder> *nodeBinder, sp<IOMXNode> *omxNode) {
    Mutex::Autolock autoLock(mLock);

    omxNode->clear();
    if (nodeBinder != NULL) {
        *nodeBinder = NULL;
    }

    if (mLiveNodes.size() == kMaxNodeInstances) {
        return NO_MEMORY;
    }

    sp<OMXNodeInstance> instance = new OMXNodeInstance(this, observer, name);

    OMX_COMPONENTTYPE *handle;
    OMX_ERRORTYPE err = mMaster->makeComponentInstance(
            name, &OMXNodeInstance::kCallbacks,
            instance.get(), &handle);

    if (err != OMX_ErrorNone) {
        ALOGE("FAILED to allocate omx component '%s' err=%s(%#x)", name, asString(err), err);

        return StatusFromOMXError(err);
    }
    instance->setHandle(handle);

    mLiveNodes.add(IInterface::asBinder(observer), instance);
    IInterface::asBinder(observer)->linkToDeath(this);

    *omxNode = instance;

    return OK;
}

status_t OMX::freeNode(const sp<OMXNodeInstance> &instance) {
    if (instance == NULL) {
        return OK;
    }

    {
        Mutex::Autolock autoLock(mLock);
        ssize_t index = mLiveNodes.indexOfKey(IInterface::asBinder(instance->observer()));
        if (index < 0) {
            // This could conceivably happen if the observer dies at roughly the
            // same time that a client attempts to free the node explicitly.
            return OK;
        }
        mLiveNodes.removeItemsAt(index);
    }

    IInterface::asBinder(instance->observer())->unlinkToDeath(this);

    OMX_ERRORTYPE err = OMX_ErrorNone;
    if (instance->handle() != NULL) {
        err = mMaster->destroyComponentInstance(
                static_cast<OMX_COMPONENTTYPE *>(instance->handle()));
    }

    return StatusFromOMXError(err);
}

status_t OMX::createPersistentInputSurface(
        sp<IGraphicBufferProducer> *bufferProducer,
        sp<IGraphicBufferConsumer> *bufferConsumer) {
    return OMXNodeInstance::createPersistentInputSurface(
            bufferProducer, bufferConsumer);
}

}  // namespace android
