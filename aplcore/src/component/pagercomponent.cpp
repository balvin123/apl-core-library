/**
 * Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "apl/command/setpagecommand.h"
#include "apl/component/componentpropdef.h"
#include "apl/component/pagercomponent.h"
#include "apl/component/yogaproperties.h"
#include "apl/content/rootconfig.h"
#include "apl/time/sequencer.h"
#include "apl/primitives/keyboard.h"

namespace apl {


CoreComponentPtr
PagerComponent::create(const ContextPtr& context,
                       Properties&& properties,
                       const std::string& path) {
    auto ptr = std::make_shared<PagerComponent>(context, std::move(properties), path);
    ptr->initialize();
    return ptr;
}

PagerComponent::PagerComponent(const ContextPtr& context,
                               Properties&& properties,
                               const std::string& path)
        : ActionableComponent(context, std::move(properties), path) {
}

inline Object
defaultWidth(Component& component, const RootConfig& rootConfig) {
    return rootConfig.getDefaultComponentWidth(component.getType());
}

inline Object
defaultHeight(Component& component, const RootConfig& rootConfig) {
    return rootConfig.getDefaultComponentHeight(component.getType());
}

const ComponentPropDefSet&
PagerComponent::propDefSet() const {
    static ComponentPropDefSet sPagerComponentProperties(ActionableComponent::propDefSet(), {
            {kPropertyHeight,        Dimension(100),        asNonAutoDimension, kPropIn, yn::setHeight, defaultHeight},
            {kPropertyWidth,         Dimension(100),        asNonAutoDimension, kPropIn, yn::setWidth,  defaultWidth},
            {kPropertyInitialPage,   0,                     asInteger,          kPropIn},
            {kPropertyNavigation,    kNavigationWrap,       sNavigationMap,     kPropInOut},
            {kPropertyOnPageChanged, Object::EMPTY_ARRAY(), asCommand,          kPropIn},
            {kPropertyCurrentPage,   0,                     asInteger,          kPropRuntimeState}
    });

    return sPagerComponentProperties;
}

void
PagerComponent::initialize() {
    CoreComponent::initialize();

    // Set current page. It will be clipped, if required, later.
    int currentPage = getCalculated(kPropertyInitialPage).asInt();
    mCalculated.set(kPropertyCurrentPage, currentPage);
}

void
PagerComponent::update(UpdateType type, float value) {
    if (type == kUpdatePagerPosition || type == kUpdatePagerByEvent) {
        int requestedPage = value;
        int currentPage = pagePosition();
        if (requestedPage != currentPage) {
            mCalculated.set(kPropertyCurrentPage, requestedPage);
            if (attachCurrentAndReportLoaded()) {
                auto width = YGNodeLayoutGetWidth(mYGNodeRef);
                auto height = YGNodeLayoutGetHeight(mYGNodeRef);
                layout(width, height, true);
            }
            ContextPtr eventContext = createEventContext("Page", requestedPage);
            mContext->sequencer().executeCommands(
                    getCalculated(kPropertyOnPageChanged),
                    eventContext,
                    shared_from_this(),
                    type == kUpdatePagerByEvent);  // If the user set the pager, run in fast mode.
        }

    } else
        CoreComponent::update(type, value);
}

const ComponentPropDefSet*
PagerComponent::layoutPropDefSet() const {
    static ComponentPropDefSet sPagerChildProperties = ComponentPropDefSet().add(
        {
            // Force absolute position because the pager children each occupy the entire space of their parent
            {kPropertyPosition, kPositionAbsolute, sPositionMap, kPropOut | kPropResetOnRemove, yn::setPositionType},

            // The width and height of the children of a pager are set to 100%
            {kPropertyWidth, Dimension(DimensionType::Relative, 100), asNonAutoDimension, kPropNone, yn::setWidth },
            {kPropertyHeight, Dimension(DimensionType::Relative, 100), asNonAutoDimension, kPropNone, yn::setHeight}
        });

    return &sPagerChildProperties;
}


std::shared_ptr<ObjectMap>
PagerComponent::getEventTargetProperties() const {
    auto target = CoreComponent::getEventTargetProperties();
    target->emplace("page", pagePosition());
    return target;
}

PageDirection
PagerComponent::pageDirection() const {
    // If we don't have children, there is no navigation
    auto len = mChildren.size();
    if (len <= 1)
        return kPageDirectionNone;

    auto navigation = static_cast<Navigation>(mCalculated.get(kPropertyNavigation).asInt());
    int currentPage = pagePosition();
    switch (navigation) {
        case kNavigationNormal:  // No wrapping, forward or back supported
            if (currentPage == 0)
                return kPageDirectionForward;
            if (currentPage == len - 1)
                return kPageDirectionBack;
            return kPageDirectionBoth;

        case kNavigationNone:
            return kPageDirectionNone;

        case kNavigationWrap:
            return kPageDirectionBoth;

        case kNavigationForwardOnly:
            if (currentPage == len - 1)
                return kPageDirectionNone;

            return kPageDirectionForward;
    }
}

std::map<int, float>
PagerComponent::getChildrenVisibility(float realOpacity, const Rect& visibleRect) {
    std::map<int, float> result;

    if (!mChildren.empty()) {
        int currentPage = pagePosition();
        auto child = mChildren.at(currentPage);
        auto childVisibility = child->calculateVisibility(realOpacity, visibleRect);
        if (childVisibility > 0.0) {
            result.emplace(currentPage, childVisibility);
        }
    }

    return result;
}

bool
PagerComponent::getTags(rapidjson::Value& outMap, rapidjson::Document::AllocatorType& allocator) {
    bool actionable = CoreComponent::getTags(outMap, allocator);
    if (mChildren.size() > 1) {
        bool allowForward = false;
        bool allowBackwards = false;

        switch (pageDirection()) {
            case kPageDirectionBoth:
                allowForward = true;
                allowBackwards = true;
                break;
            case kPageDirectionBack:
                allowBackwards = true;
                break;
            case kPageDirectionForward:
                allowForward = true;
                break;
            case kPageDirectionNone:
                break;
            default:
                break;
        }

        rapidjson::Value pager(rapidjson::kObjectType);
        pager.AddMember("index", pagePosition(), allocator);
        pager.AddMember("pageCount", (int) mChildren.size(), allocator);
        pager.AddMember("allowForward", allowForward, allocator);
        pager.AddMember("allowBackwards", allowBackwards, allocator);
        outMap.AddMember("pager", pager, allocator);
        actionable = true;
    }

    return actionable;
}

ComponentPtr
PagerComponent::findChildAtPosition(const Point& position) const {
    // The current page may contain the target position
    int currentPage = pagePosition();
    if (currentPage >= 0 && currentPage < getChildCount()) {
        auto child = mChildren.at(currentPage)->findComponentAtPosition(position);
        if (child != nullptr)
            return child;
    }

    return nullptr;
}

bool
PagerComponent::insertChild(const ComponentPtr& child, size_t index, bool useDirtyFlag) {
    size_t initialSize = mChildren.size();
    bool result = CoreComponent::insertChild(child, index, useDirtyFlag);
    if (result) {
        int currentPage = getCalculated(kPropertyCurrentPage).asInt();
        if (currentPage >= index && index < initialSize) {
            mCalculated.set(kPropertyCurrentPage, currentPage + 1);
            setDirty(kPropertyCurrentPage);
        }
    }
    return result;
}

void
PagerComponent::removeChild(const CoreComponentPtr& child, size_t index, bool useDirtyFlag) {
    CoreComponent::removeChild(child, index, useDirtyFlag);
    int currentPage = getCalculated(kPropertyCurrentPage).asInt();
    if (currentPage >= index && currentPage != 0) {
        mCalculated.set(kPropertyCurrentPage, currentPage - 1);
        setDirty(kPropertyCurrentPage);
    }
}

void
PagerComponent::processLayoutChanges(bool useDirtyFlag) {
    attachCurrentAndReportLoaded();
    CoreComponent::processLayoutChanges(useDirtyFlag);
}

bool
PagerComponent::shouldAttachChildYogaNode(int index) const {
    auto navigation = static_cast<Navigation>(mCalculated.get(kPropertyNavigation).asInt());

    // Always attach for wrapping case as otherwise it will not actually be possible.
    // Do not do that for dynamic source though as if it's not fully loaded wrap is unclear.
    if (!mRebuilder && kNavigationWrap == navigation) {
        return true;
    }

    // Only attach initial page as any cache required will be attached later.
    int currentPage = getCalculated(kPropertyCurrentPage).asInt();
    return mEnsuredChildren.empty() && index == currentPage;
}

void
PagerComponent::finalizePopulate() {
    // Take this chance to set initial page as visible (and clip it if required).
    int initialPage = getCalculated(kPropertyInitialPage).asInt();
    initialPage = std::max(initialPage, 0);

    if (!mChildren.empty()) {
        initialPage = std::min(initialPage, (int)mChildren.size() - 1);
    } else {
        initialPage = 0;
    }

    mCalculated.set(kPropertyCurrentPage, initialPage);
    attachCurrentAndReportLoaded();

    auto navigation = static_cast<Navigation>(mCalculated.get(kPropertyNavigation).asInt());

    // Override wrap to normal if dynamic data.
    if (mRebuilder && navigation == kNavigationWrap ) {
        mCalculated.set(kPropertyNavigation, kNavigationNormal);
    }
}

bool
PagerComponent::attachCurrentAndReportLoaded() {
    if (mChildren.empty()) {
        return false;
    }

    int currentPage = getCalculated(kPropertyCurrentPage).asInt();
    int childCache = mContext->getRootConfig().getPagerChildCache();
    int lowerBound = currentPage - childCache;
    int upperBound = currentPage + childCache;
    size_t size = mChildren.size();
    bool needsLayoutCalculation = false;

    if (!mChildren.at(currentPage)->isAttached()) {
        ensureChildAttached(mChildren.at(currentPage), currentPage);
        needsLayoutCalculation = true;
    }
    if (lowerBound >= 0 && !mChildren.at(lowerBound)->isAttached()) {
        ensureChildAttached(mChildren.at(lowerBound), lowerBound);
        needsLayoutCalculation = true;
    }
    if (upperBound < size && !mChildren.at(upperBound)->isAttached()) {
        ensureChildAttached(mChildren.at(upperBound), upperBound);
        needsLayoutCalculation = true;
    }

    reportLoaded(currentPage);

    return needsLayoutCalculation;
}


} // namespace apl
