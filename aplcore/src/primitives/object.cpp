/**
 * Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <cmath>
#include <clocale>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "apl/datagrammar/boundsymbol.h"
#include "apl/datagrammar/node.h"
#include "apl/graphic/graphic.h"
#include "apl/primitives/color.h"
#include "apl/primitives/dimension.h"
#include "apl/primitives/filter.h"
#include "apl/primitives/functions.h"
#include "apl/livedata/livedataobject.h"
#include "apl/primitives/object.h"
#include "apl/primitives/mediasource.h"
#include "apl/primitives/transform.h"
#include "apl/utils/log.h"

namespace apl {

const bool OBJECT_DEBUG = false;

/****************************************************************************/

class ArrayData : public Object::Data {
public:
    ArrayData(const ObjectArrayPtr& array, bool isMutable) : mArray(array), mIsMutable(isMutable) {
        assert(array);
    }

    const Object
    at(size_t index) const override
    {
        size_t len = mArray->size();
        if (index >= len)
            return Object::NULL_OBJECT();

        return mArray->at(index);
    }

    size_t size() const override { return mArray->size(); }
    bool empty() const override { return mArray->empty(); }

    bool isMutable() const override { return mIsMutable; }

    void
    accept(Visitor<Object>& visitor) const override
    {
        visitor.push();
        for (auto it = mArray->begin() ; !visitor.isAborted() && it != mArray->end() ; it++)
            it->accept(visitor);
        visitor.pop();
    }

    const std::vector<Object>& getArray() const override {
        return *mArray;
    }

    std::vector<Object>& getMutableArray() override {
        if (!mIsMutable)
            throw std::runtime_error("Attempted to retrieve mutable array for non-mutable object");
        return *mArray;
    }

    std::string toDebugString() const override {
        std::string result = "Array<size=" + std::to_string(mArray->size()) + ">[";
        for (const auto& m : *mArray) {
            result += m.toDebugString();
            result += ", ";
        }
        result += "]";

        return result;
    }

private:
    ObjectArrayPtr mArray;
    bool mIsMutable;
};

/****************************************************************************/

class FixedArrayData : public Object::Data {
public:
    FixedArrayData(ObjectArray&& array, bool isMutable) : mArray(std::move(array)), mIsMutable(isMutable) {}

    const Object
    at(size_t index) const override
    {
        size_t len = mArray.size();
        if (index >= len)
            return Object::NULL_OBJECT();

        return mArray.at(index);
    }

    size_t size() const override {
        return mArray.size();
    }

    bool empty() const override {
        return mArray.empty();
    }

    bool isMutable() const override {
        return mIsMutable;
    }

    void
    accept(Visitor<Object>& visitor) const override
    {
        visitor.push();
        for (auto it = mArray.begin() ; !visitor.isAborted() && it != mArray.end() ; it++)
            it->accept(visitor);
        visitor.pop();
    }

    const std::vector<Object>& getArray() const override {
        return mArray;
    }

    std::vector<Object>& getMutableArray() override {
        if (!mIsMutable)
            throw std::runtime_error("Attempted to retrieve mutable array for non-mutable object");
        return mArray;
    }

    std::string toDebugString() const override {
        std::string result = "FixedArray<size=" + std::to_string(mArray.size()) + ">[";
        for (const auto& m : mArray) {
            result += m.toDebugString();
            result += ", ";
        }
        result += "]";

        return result;
    }

private:
    ObjectArray mArray;
    bool mIsMutable;
};

/****************************************************************************/

class MapData : public Object::Data {
public:
    explicit MapData(const std::shared_ptr<ObjectMap>& map, bool isMutable) : mMap(map), mIsMutable(isMutable) {
        assert(map);
    }

    const Object
    get(const std::string& key) const override
    {
        auto it = mMap->find(key);
        if (it != mMap->end())
            return it->second;
        return Object::NULL_OBJECT();
    }

    const Object
    opt(const std::string& key, const Object& def) const override
    {
        auto it = mMap->find(key);
        if (it != mMap->end())
            return it->second;
        return def;
    }

    size_t size() const override { return mMap->size(); }
    bool empty() const override { return mMap->empty(); }
    bool isMutable() const override { return mIsMutable; }
    bool has(const std::string& key) const override { return mMap->count(key) != 0; }

    const ObjectMap& getMap() const override {
        return *mMap;
    }

    ObjectMap& getMutableMap() override {
        if (!mIsMutable)
            throw std::runtime_error("Attempted to retrieve mutable map for non-mutable object");
        return *mMap;
    }

    void
    accept(Visitor<Object>& visitor) const override
    {
        visitor.push();
        for (auto it = mMap->begin() ; !visitor.isAborted() && it != mMap->end() ; it++) {
            Object(it->first).accept(visitor);
            if (!visitor.isAborted()) {
                visitor.push();
                it->second.accept(visitor);
                visitor.pop();
            }
        }
        visitor.pop();
    }

    std::string toDebugString() const override {
        std::string result = "Map<size=" + std::to_string(mMap->size()) + ">[";
        for (const auto& m : *mMap) {
            result += "{" + m.first + "," + m.second.toDebugString() + "}, ";
        }
        result += "]";
        return result;
    }
private:
    ObjectMapPtr mMap;
    bool mIsMutable;
};


/****************************************************************************/

class JSONData : public Object::Data {
public:
    JSONData(const rapidjson::Value *value) : mValue(value) { assert(value); }

    const Object
    get(const std::string& key) const override
    {
        if (mValue->IsObject()) {
            auto it = mValue->FindMember(key.c_str());
            if (it != mValue->MemberEnd())
                return it->value;
        }
        return Object::NULL_OBJECT();
    }

    const Object
    opt(const std::string& key, const Object& def) const override
    {
        if (mValue->IsObject()) {
            auto it = mValue->FindMember(key.c_str());
            if (it != mValue->MemberEnd())
                return it->value;
        }
        return def;
    }

    bool
    has(const std::string& key) const override {
        if (!mValue->IsObject())
            return false;

        return mValue->FindMember(key.c_str()) != mValue->MemberEnd();
    }

    const Object
    at(size_t index) const override {
        if (!mValue->IsArray() || index >= mValue->Size())
            return Object::NULL_OBJECT();
        return (*mValue)[index];
    }

    size_t
    size() const override {
        if (mValue->IsArray())
            return mValue->Size();
        if (mValue->IsObject())
            return mValue->MemberCount();
        return 0;
    }

    bool
    empty() const override {
        if (mValue->IsArray())
            return mValue->Empty();
        if (mValue->IsObject())
            return mValue->MemberCount() == 0;
        return false;
    }

    const std::vector<Object>& getArray() const override {
        assert(mValue->IsArray());

        if (mValue->Size() != mVector.size())
            for (const auto& v : mValue->GetArray())
                const_cast<std::vector<Object>&>(mVector).emplace_back(v);
        return mVector;
    }

    const std::map<std::string, Object>& getMap() const override {
        assert(mValue->IsObject());

        if (mValue->MemberCount() != mMap.size())
            for (const auto& v : mValue->GetObject())
                const_cast<std::map<std::string, Object>&>(mMap).emplace(v.name.GetString(), v.value);
        return mMap;
    }

    const rapidjson::Value* getJson() const override {
        return mValue;
    }

    std::string toDebugString() const override {
        rapidjson::StringBuffer buffer;
        buffer.Clear();
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        mValue->Accept(writer);
        return "JSON<" + std::string(buffer.GetString()) + ">";
    }

private:
    const rapidjson::Value *mValue;
    const std::map<std::string, Object> mMap;
    const std::vector<Object> mVector;
};

/****************************************************************************/

class JSONDocumentData : public Object::Data {
public:
    JSONDocumentData(rapidjson::Document&& doc) : mDoc(std::move(doc)) {}

    const Object
    get(const std::string& key) const override
    {
        if (mDoc.IsObject()) {
            auto it = mDoc.FindMember(key.c_str());
            if (it != mDoc.MemberEnd())
                return it->value;
        }
        return Object::NULL_OBJECT();
    }

    const Object
    opt(const std::string& key, const Object& def) const override
    {
        if (mDoc.IsObject()) {
            auto it = mDoc.FindMember(key.c_str());
            if (it != mDoc.MemberEnd())
                return it->value;
        }
        return def;
    }

    bool
    has(const std::string& key) const override {
        if (!mDoc.IsObject())
            return false;

        return mDoc.FindMember(key.c_str()) != mDoc.MemberEnd();
    }

    const Object
    at(size_t index) const override {
        if (!mDoc.IsArray() || index >= mDoc.Size())
            return Object::NULL_OBJECT();
        return mDoc[index];
    }

    size_t
    size() const override {
        if (mDoc.IsArray())
            return mDoc.Size();
        if (mDoc.IsObject())
            return mDoc.MemberCount();
        return 0;
    }

    bool
    empty() const override {
        if (mDoc.IsArray())
            return mDoc.Empty();
        if (mDoc.IsObject())
            return mDoc.MemberCount() == 0;
        return false;
    }

    const std::vector<Object>& getArray() const override {
        assert(mDoc.IsArray());

        if (mDoc.Size() != mVector.size())
            for (const auto& v : mDoc.GetArray())
                const_cast<std::vector<Object>&>(mVector).emplace_back(v);
        return mVector;
    }

    const std::map<std::string, Object>& getMap() const override {
        assert(mDoc.IsObject());

        if (mDoc.MemberCount() != mMap.size())
            for (const auto& v : mDoc.GetObject())
                const_cast<std::map<std::string, Object>&>(mMap).emplace(v.name.GetString(), v.value);
        return mMap;
    }

    const rapidjson::Value* getJson() const override {
        return &mDoc;
    }

    std::string toDebugString() const override {
        return "JSONDoc<size=" + std::to_string(size()) + ">";
    }

private:
    const rapidjson::Document mDoc;
    const std::map<std::string, Object> mMap;
    const std::vector<Object> mVector;
};

/****************************************************************************/

class FilterData : public Object::Data {
public:
    FilterData(Filter&& filter) : mFilter(std::move(filter)) {}
    const Filter& getFilter() const override { return mFilter; }

    std::string toDebugString() const override {
        return "Filter<>";
    }

private:
    Filter mFilter;
};

/****************************************************************************/

class GradientData : public Object::Data {
public:
    GradientData(Gradient&& gradient) : mGradient(std::move(gradient)) {}
    const Gradient& getGradient() const override { return mGradient; }

    std::string toDebugString() const override {
        return "Gradient<>";
    }

private:
    Gradient mGradient;
};


/****************************************************************************/

class MediaSourceData : public Object::Data {
public:
    MediaSourceData(MediaSource&& mediaSource) : mMediaSource(std::move(mediaSource)) {}

    const MediaSource& getMediaSource() const override { return mMediaSource; }

    std::string toDebugString() const override {
        return "MediaSource<>";
    }

private:
    MediaSource mMediaSource;
};

/****************************************************************************/

class RectData : public Object::Data {
public:
    RectData(Rect&& rect) : mRect(std::move(rect)) {}
    Rect getRect() const override { return mRect; }

    bool empty() const override { return mRect.isEmpty(); }

    std::string toDebugString() const override {
        return "Rect<"+mRect.toString()+">";
    }
private:
    Rect mRect;
};

/****************************************************************************/

class RadiiData : public Object::Data {
public:
    RadiiData(Radii&& radii) : mRadii(std::move(radii)) {}
    Radii getRadii() const override { return mRadii; }

    std::string toDebugString() const override {
        return "Radii<"+mRadii.toString()+">";
    }
private:
    Radii mRadii;
};

/****************************************************************************/

class StyledTextData : public Object::Data {
public:
    StyledTextData(StyledText&& styledText) : mStyledText(std::move(styledText)) {}

    const StyledText& getStyledText() const override {
        return mStyledText;
    }

    std::string toDebugString() const override {
        return "StyledText<"+mStyledText.asString()+">";
    }

private:
    StyledText mStyledText;
};

/****************************************************************************/

class GraphicData : public Object::Data {
public:
    GraphicData(const GraphicPtr graphic) : mGraphic(graphic) {}
    GraphicPtr getGraphic() const override { return mGraphic; }

    std::string toDebugString() const override {
        return "Graphic<>";
    }

private:
    const GraphicPtr mGraphic;
};

/****************************************************************************/

class TransformData : public Object::Data {
public:
    TransformData(const std::shared_ptr<Transformation> transform) : mTransform(transform) {}
    std::shared_ptr<Transformation> getTransform() const override { return mTransform; }

    std::string toDebugString() const override {
        return "Transform<>";
    }

private:
    std::shared_ptr<Transformation> mTransform;
};

/****************************************************************************/

class Transform2DData : public Object::Data {
public:
    Transform2DData(Transform2D&& transform) : mTransform(std::move(transform)) {}
    Transform2D getTransform2D() const override { return mTransform; }

    std::string toDebugString() const override {
        streamer s;
        s << mTransform;
        return "Transform2D<"+s.str()+">";
    }

private:
    const Transform2D mTransform;
};

/****************************************************************************/

class EasingData : public Object::Data {
public:
    EasingData(Easing&& easing) : mEasing(std::move(easing)) {}
    Easing getEasing() const override { return mEasing; }

    std::string toDebugString() const override {
        return "Easing<>";
    }

private:
    const Easing mEasing;
};

/****************************************************************************/

template<typename XData, typename T>
std::shared_ptr<Object::Data> create(T&& data) {
    return std::make_shared<XData>(std::move(data));
}

/****************************************************************************/

const Object& Object::TRUE_OBJECT() {
    static Object *answer = new Object(true);
    return *answer;
}

const Object& Object::FALSE_OBJECT() {
    static Object *answer = new Object(false);
    return *answer;
}

const Object& Object::NULL_OBJECT() {
    static Object *answer = new Object();
    return *answer;
}

Object Object::NAN_OBJECT() {
    return Object(std::numeric_limits<double>::quiet_NaN());
}

Object Object::AUTO_OBJECT() {
    return Object(Dimension());
}

Object Object::EMPTY_ARRAY() {
    return Object(std::vector<Object>{});
}

Object Object::EMPTY_MUTABLE_ARRAY() {
    return Object(std::vector<Object>{}, true);
}

Object Object::EMPTY_MAP() {
    return Object(std::make_shared<ObjectMap>());
}

Object Object::EMPTY_MUTABLE_MAP() {
    return Object(std::make_shared<ObjectMap>(), true);
}

Object Object::ZERO_ABS_DIMEN() {
    return Object(Dimension(DimensionType::Absolute, 0));
}

Object Object::EMPTY_RECT() {
    return Object(Rect(0,0,0,0));
}

Object Object::EMPTY_RADII() {
    return Object(Radii());
}

Object Object::IDENTITY_2D() {
    return Object(Transform2D());
}

Object Object::LINEAR_EASING() {
    return Object(Easing::linear());
}

/****************************************************************************/

Object::Object(const Object& object) noexcept
        : mType(object.mType)
{
    switch (mType) {
        case kBoolType:
        case kNumberType:
        case kAbsoluteDimensionType:
        case kRelativeDimensionType:
        case kAutoDimensionType:
        case kColorType:
            mValue = object.mValue;
            break;
        case kStringType:
            mString = object.mString;
            break;
        default:
            mData = object.mData;
            break;
    }
}

Object::Object(Object&& object) noexcept
        : mType(object.mType)
{
    switch (mType) {
        case kBoolType:
        case kNumberType:
        case kAbsoluteDimensionType:
        case kRelativeDimensionType:
        case kAutoDimensionType:
        case kColorType:
            mValue = object.mValue;
            break;
        case kStringType:
            mString = std::move(object.mString);
            break;
        default:
            mData = std::move(object.mData);
            break;
    }
}

Object& Object::operator=(const Object& rhs) noexcept
{
    if (this == &rhs)
        return *this;

    mType = rhs.mType;
    switch (mType) {
        case kBoolType:
        case kNumberType:
        case kAbsoluteDimensionType:
        case kRelativeDimensionType:
        case kAutoDimensionType:
        case kColorType:
            mValue = rhs.mValue;
            break;
        case kStringType:
            mString = rhs.mString;
            break;
        default:
            mData = rhs.mData;
            break;
    }

    return *this;
}

Object& Object::operator=(Object&& rhs) noexcept
{
    mType = rhs.mType;
    switch (mType) {
        case kBoolType:
        case kNumberType:
        case kAbsoluteDimensionType:
        case kRelativeDimensionType:
        case kAutoDimensionType:
        case kColorType:
            mValue = rhs.mValue;
            break;
        case kStringType:
            mString = std::move(rhs.mString);
            break;
        default:
            mData = std::move(rhs.mData);
            break;
    }

    return *this;
}

/****************************************************************************/

Object::~Object() {
    LOG_IF(OBJECT_DEBUG) << "  --- Destroying " << *this;
}

Object::Object()
    : mType(kNullType)
{
    LOG_IF(OBJECT_DEBUG) << "Object null constructor" << this;
}

Object::Object(ObjectType type)
    : mType(type)
{
    LOG_IF(OBJECT_DEBUG) << "Object type constructor" << this;
}

Object::Object(bool b)
    : mType(kBoolType),
      mValue(b ? 1 : 0)
{
    LOG_IF(OBJECT_DEBUG) << "Object bool constructor: " << b << " this=" << this;
}

Object::Object(int i)
    : mType(kNumberType),
      mValue(i)
{}

Object::Object(uint32_t u)
    : mType(kNumberType),
      mValue(u)
{}

Object::Object(unsigned long l)
    : mType(kNumberType),
      mValue(l)
{}

Object::Object(long l)
    : mType(kNumberType),
      mValue(l)
{}

Object::Object(unsigned long long l)
    : mType(kNumberType),
      mValue(l)
{}

Object::Object(long long l)
    : mType(kNumberType),
      mValue(l)
{}

Object::Object(double d)
    : mType(kNumberType),
      mValue(d)
{}

Object::Object(const char *s)
    : mType(kStringType),
      mString(s)
{}

Object::Object(const std::string& s)
    : mType(kStringType),
      mString(s)
{}

Object::Object(const ObjectMapPtr& m, bool isMutable)
    : mType(kMapType),
      mData(std::static_pointer_cast<Data>(std::make_shared<MapData>(m, isMutable)))
{}

Object::Object(const ObjectArrayPtr& v, bool isMutable)
    : mType(kArrayType),
      mData(std::static_pointer_cast<Data>(std::make_shared<ArrayData>(v, isMutable)))
{}

Object::Object(ObjectArray&& v, bool isMutable)
    : mType(kArrayType),
      mData(std::static_pointer_cast<Data>(std::make_shared<FixedArrayData>(std::move(v),isMutable)))
{}

Object::Object(const std::shared_ptr<datagrammar::Node>& n)
    : mType(kNodeType),
      mData(std::static_pointer_cast<Data>(n))
{
    LOG_IF(OBJECT_DEBUG) << "Object constructor node: " << this;
}

Object::Object(const std::shared_ptr<datagrammar::BoundSymbol>& bs)
    : mType(kBoundSymbolType),
      mData(std::static_pointer_cast<Data>(bs))
{
    LOG_IF(OBJECT_DEBUG) << "Object constructor bound symbol: " << this;
}

Object::Object(const std::shared_ptr<LiveDataObject>& d)
    : mType(d->getType()),
      mData(d)
{
    LOG_IF(OBJECT_DEBUG) << "Object live data: " << this;
}

Object::Object(const rapidjson::Value& value)
    : mValue(0)
{
    LOG_IF(OBJECT_DEBUG) << "Object constructor value: " << this;

    switch(value.GetType()) {
    case rapidjson::kNullType:
        mType = kNullType;
        break;
    case rapidjson::kFalseType:
        mType = kBoolType;
        mValue = 0;
        break;
    case rapidjson::kTrueType:
        mType = kBoolType;
        mValue = 1;
        break;
    case rapidjson::kNumberType:
        mType = kNumberType;
        mValue = value.GetDouble();
        break;
    case rapidjson::kStringType:
        mType = kStringType;
        mString = value.GetString();  // TODO: Should we keep the string in place?
        break;
    case rapidjson::kObjectType:
        mType = kMapType;
        mData = std::static_pointer_cast<Data>(std::make_shared<JSONData>(&value));
        break;
    case rapidjson::kArrayType:
        mType = kArrayType;
        mData = std::static_pointer_cast<Data>(std::make_shared<JSONData>(&value));
        break;
    }
}

Object::Object(rapidjson::Document&& value)
    : mValue(0)
{
    if (OBJECT_DEBUG) LOG(LogLevel::DEBUG) << "Object constructor value: " << this;

    switch(value.GetType()) {
        case rapidjson::kNullType:
            mType = kNullType;
            break;
        case rapidjson::kFalseType:
            mType = kBoolType;
            mValue = 0;
            break;
        case rapidjson::kTrueType:
            mType = kBoolType;
            mValue = 1;
            break;
        case rapidjson::kNumberType:
            mType = kNumberType;
            mValue = value.GetDouble();
            break;
        case rapidjson::kStringType:
            mType = kStringType;
            mString = value.GetString();  // TODO: Should we keep the string in place?
            break;
        case rapidjson::kObjectType:
            mType = kMapType;
            mData = std::static_pointer_cast<Data>(std::make_shared<JSONDocumentData>(std::move(value)));
            break;
        case rapidjson::kArrayType:
            mType = kArrayType;
            mData = std::static_pointer_cast<Data>(std::make_shared<JSONDocumentData>(std::move(value)));
            break;
    }
}

Object::Object(const std::shared_ptr<Function>& f)
    : mType(kFunctionType),
      mData(std::static_pointer_cast<Data>(f))
{
    LOG_IF(OBJECT_DEBUG) << "User Function constructor";
}

Object::Object(const Dimension& d)
    : mType(d.isAuto() ? kAutoDimensionType :
            (d.isRelative() ? kRelativeDimensionType : kAbsoluteDimensionType)),
      mValue(d.getValue())
{
    if (OBJECT_DEBUG)
        LOG(LogLevel::DEBUG) << "Object dimension constructor: " << this << " is " << *this << " dimension=" << d;
}

Object::Object(const Color& color)
    : mType(kColorType),
      mValue(color.get())
{
    LOG_IF(OBJECT_DEBUG) << "Object color constructor " << this;
}

Object::Object(Filter&& filter)
    : mType(kFilterType),
      mData(create<FilterData, Filter>(std::move(filter)))
{
    LOG_IF(OBJECT_DEBUG) << "Object filter constructor " << this;
}

Object::Object(Gradient&& gradient)
    : mType(kGradientType),
      mData(create<GradientData, Gradient>(std::move(gradient)))
{
    LOG_IF(OBJECT_DEBUG) << "Object gradient constructor " << this;
}


Object::Object(MediaSource&& mediaSource)
    : mType(kMediaSourceType),
      mData(create<MediaSourceData, MediaSource>(std::move(mediaSource)))
{
    LOG_IF(OBJECT_DEBUG) << "Object MediaSource constructor " << this;
}

Object::Object(Rect&& rect)
    : mType(kRectType),
      mData(create<RectData, Rect>(std::move(rect)))
{
    LOG_IF(OBJECT_DEBUG) << "Object Rect constructor " << this;
}

Object::Object(Radii&& radii)
    : mType(kRadiiType),
      mData(create<RadiiData, Radii>(std::move(radii)))
{
    LOG_IF(OBJECT_DEBUG) << "Object Radii constructor " << this;
}

Object::Object(StyledText&& styledText)
    : mType(kStyledTextType),
      mData(create<StyledTextData, StyledText>(std::move(styledText)))
{
    LOG_IF(OBJECT_DEBUG) << "Object StyledText constructor " << this;
}

Object::Object(const GraphicPtr& graphic)
    : mType(kGraphicType),
      mData(std::make_shared<GraphicData>(graphic))
{
    LOG_IF(OBJECT_DEBUG) << "Object Graphic constructor " << this;
}

Object::Object(const std::shared_ptr<Transformation>& transform)
    : mType(kTransformType),
      mData(std::make_shared<TransformData>(transform))
{
    LOG_IF(OBJECT_DEBUG) << "Object transform constructor " << this;
}

Object::Object(Transform2D&& transform)
    : mType(kTransform2DType),
      mData(create<Transform2DData, Transform2D>(std::move(transform)))
{
    LOG_IF(OBJECT_DEBUG) << "Object transform 2D constructor " << this;
}

Object::Object(Easing&& easing)
    : mType(kEasingType),
      mData(create<EasingData, Easing>(std::move(easing)))
{
    LOG_IF(OBJECT_DEBUG) << "Object easing constructor " << this;
}

bool
Object::operator==(const Object& rhs) const
{
    LOG_IF(OBJECT_DEBUG) << "comparing " << *this << " to " << rhs;

    if (mType != rhs.mType)
        return false;

    switch (mType) {
        case kNullType:
        case kAutoDimensionType:
            return true;

        case kBoolType:
        case kNumberType:
        case kAbsoluteDimensionType:
        case kRelativeDimensionType:
        case kColorType:
            return mValue == rhs.mValue;

        case kStringType:
            return mString == rhs.mString;

        case kMapType: {
            if (mData->size() != rhs.mData->size())
                return false;

            auto left = mData->getMap();
            auto right = rhs.mData->getMap();
            for (auto& m : left) {
                auto it = right.find(m.first);
                if (it == right.end())
                    return false;
                if (m.second != it->second)
                    return false;
            }
            return true;
        }

        case kArrayType: {
            if (mData->size() != rhs.mData->size())
                return false;

            auto left = mData->getArray();
            auto right = rhs.mData->getArray();
            for (int i = 0 ; i < mData->size() ; i++)
                if (left.at(i) != right.at(i))
                    return false;

            return true;
        }

        case kNodeType:
        case kFunctionType:
        case kGradientType:
            return mData == rhs.mData;
        case kFilterType:
            return mData->getFilter() == rhs.mData->getFilter();
        case kMediaSourceType:
            return mData == rhs.mData;
        case kRectType:
            return mData->getRect() == rhs.mData->getRect();
        case kRadiiType:
            return mData->getRadii() == rhs.mData->getRadii();
        case kStyledTextType:
            return mData->getStyledText() == rhs.mData->getStyledText();
        case kGraphicType:
            return mData == rhs.mData;
        case kTransformType:
            return mData == rhs.mData;
        case kTransform2DType:
            return mData->getTransform2D() == rhs.mData->getTransform2D();
        case kEasingType:
            return mData->getEasing() == rhs.mData->getEasing();
        case kBoundSymbolType:
            return mData == rhs.mData;
    }

    return false;  // Shouldn't ever get here
}

bool
Object::operator!=(const Object& rhs) const
{
    return !operator==(rhs);
}

bool
Object::isJson() const
{
    switch(mType) {
        case kMapType:
        case kArrayType:
            return mData->getJson() != nullptr;
        default:
            return false;
    }
}

/**
 * Return an attractively formatted double for display.
 * We drop trailing zeros for decimal numbers.  If the number is an integer or rounds
 * to an integer, we drop the decimal point as well.
 * Scientific notation numbers are not handled attractively.
 * @param value The value to format
 * @return A suitable string
 */
static inline std::string
doubleToString(double value)
{
    if (value < std::numeric_limits<int>::max() && value > std::numeric_limits<int>::min()) {
        auto iValue = static_cast<int>(value);
        if (value == iValue)
            return std::to_string(iValue);
    }

    // TODO: Is this cheap enough to run each time?
    //       If so, we could unit test other languages more easily
    static char *separator = std::localeconv()->decimal_point;

    auto s = std::to_string(value);
    auto it = s.find_last_not_of('0');
    if (it != s.find(separator))   // Remove a trailing decimal point
        it++;
    s.erase(it, std::string::npos);
    return s;
}

/**
 * This method is used when coercing an object to a string.  This can be used
 * by an APL author to display information in a Text component, so we deliberately
 * do not return values for many of the internal object types.  Please use
 * Object::toDebugString() to return strings suitable for writing to the system log.
 *
 * @return The user-friendly value of the object as a string.
 */
std::string
Object::asString() const
{
    switch (mType) {
        case kNullType: return "";
        case kBoolType: return mValue ? "true": "false";
        case kStringType: return mString;
        case kNumberType: return doubleToString(mValue);
        case kAutoDimensionType: return "auto";
        case kAbsoluteDimensionType: return doubleToString(mValue)+"dp";
        case kRelativeDimensionType: return doubleToString(mValue)+"%";
        case kColorType: return Color(mValue).asString();
        case kMapType: return "";
        case kArrayType: return "";
        case kNodeType: return "";
        case kFunctionType: return "";
        case kFilterType: return "";
        case kGradientType: return "";
        case kMediaSourceType: return "";
        case kRectType: return "";
        case kRadiiType: return "";
        case kStyledTextType: return mData->getStyledText().asString();
        case kGraphicType: return "";
        case kTransformType: return "";
        case kTransform2DType: return "";
        case kEasingType: return "";
        case kBoundSymbolType: return "";
    }

    return "ERROR";  // TODO: Fix up the string type
}

inline double
stringToDouble(const std::string& string)
{
    try {
        auto len = string.size();
        auto idx = len;
        double result = std::stod(string, &idx);
        // Handle percentages.  We skip over whitespace and stop on any other character
        while (idx < len) {
            auto c = string[idx];
            if (c == '%') {
                result *= 0.01;
                break;
            }
            if (!std::isspace(c))
                break;
            idx++;
        }
        return result;
    } catch (...) {}
    return std::numeric_limits<double>::quiet_NaN();
}

double
Object::asNumber() const
{
    switch (mType) {
        case kBoolType:
        case kNumberType:
            return mValue;
        case kStringType:
            return stringToDouble(mString);
        case kStyledTextType:
            return stringToDouble(mData->getStyledText().asString());
        case kAbsoluteDimensionType:
            return mValue;
        default:
            return std::numeric_limits<double>::quiet_NaN();
    }
}

int
Object::asInt() const
{
    switch (mType) {
        case kBoolType:
            return static_cast<int>(mValue);
        case kNumberType:
            return std::rint(mValue);
        case kStringType:
            try { return std::stoi(mString); } catch(...) {}
            return std::numeric_limits<int>::quiet_NaN();
        case kStyledTextType:
            try { return std::stoi(mData->getStyledText().asString()); } catch(...) {}
            return std::numeric_limits<int>::quiet_NaN();
        case kAbsoluteDimensionType:
            return std::rint(mValue);
        default:
            return std::numeric_limits<int>::quiet_NaN();
    }
}

/// @deprecated
Color
Object::asColor() const
{
    return asColor(nullptr);
}

Color
Object::asColor(const SessionPtr& session) const
{
    switch (mType) {
        case kNumberType:
        case kColorType:
            return Color(mValue);
        case kStringType:
            return Color(session, mString);
        case kStyledTextType:
            return Color(session, mData->getStyledText().asString());
        default:
            return Color();  // Transparent
    }
}

Color
Object::asColor(const Context& context) const
{
    return asColor(context.session());
}

Dimension
Object::asDimension(const Context& context) const
{
    switch (mType) {
        case kNumberType:
            return Dimension(DimensionType::Absolute, mValue);
        case kStringType:
            return Dimension(context, mString);
        case kAbsoluteDimensionType:
            return Dimension(DimensionType::Absolute, mValue);
        case kRelativeDimensionType:
            return Dimension(DimensionType::Relative, mValue);
        case kAutoDimensionType:
            return Dimension(DimensionType::Auto, 0);
        case kStyledTextType:
            return Dimension(context, mData->getStyledText().asString());
        default:
            return Dimension(DimensionType::Absolute, 0);
    }
}

Dimension
Object::asAbsoluteDimension(const Context& context) const
{
    switch (mType) {
        case kNumberType:
            return Dimension(DimensionType::Absolute, mValue);
        case kStringType: {
            auto d = Dimension(context, mString);
            return (d.getType() == DimensionType::Absolute ? d : Dimension(DimensionType::Absolute, 0));
        }
        case kStyledTextType: {
            auto d = Dimension(context, mData->getStyledText().asString());
            return (d.getType() == DimensionType::Absolute ? d : Dimension(DimensionType::Absolute, 0));
        }
        case kAbsoluteDimensionType:
            return Dimension(DimensionType::Absolute, mValue);
        default:
            return Dimension(DimensionType::Absolute, 0);
    }
}

Dimension
Object::asNonAutoDimension(const Context& context) const
{
    switch (mType) {
        case kNumberType:
            return Dimension(DimensionType::Absolute, mValue);
        case kStringType: {
            auto d = Dimension(context, mString);
            return (d.getType() == DimensionType::Auto ? Dimension(DimensionType::Absolute, 0) : d);
        }
        case kStyledTextType: {
            auto d = Dimension(context, mData->getStyledText().asString());
            return (d.getType() == DimensionType::Auto ? Dimension(DimensionType::Absolute, 0) : d);
        }
        case kAbsoluteDimensionType:
            return Dimension(DimensionType::Absolute, mValue);
        case kRelativeDimensionType:
            return Dimension(DimensionType::Relative, mValue);
        default:
            return Dimension(DimensionType::Absolute, 0);
    }
}

Dimension
Object::asNonAutoRelativeDimension(const Context& context) const
{
    switch (mType) {
        case kNumberType:
            return Dimension(DimensionType::Relative, mValue * 100);
        case kStringType: {
            auto d = Dimension(context, mString, true);
            return (d.getType() == DimensionType::Auto ? Dimension(DimensionType::Relative, 0) : d);
        }
        case kStyledTextType: {
            auto d = Dimension(context, mData->getStyledText().asString(), true);
            return (d.getType() == DimensionType::Auto ? Dimension(DimensionType::Relative, 0) : d);
        }
        case kAbsoluteDimensionType:
            return Dimension(DimensionType::Absolute, mValue);
        case kRelativeDimensionType:
            return Dimension(DimensionType::Relative, mValue);
        default:
            return Dimension(DimensionType::Relative, 0);
    }
}

std::shared_ptr<Function>
Object::getFunction() const
{
    assert(mType == kFunctionType);
    return std::static_pointer_cast<Function>(mData);
}

std::shared_ptr<datagrammar::BoundSymbol>
Object::getBoundSymbol() const
{
    assert(mType == kBoundSymbolType);
    return std::static_pointer_cast<datagrammar::BoundSymbol>(mData);
}

std::shared_ptr<LiveDataObject>
Object::getLiveDataObject() const
{
    assert(mType == kArrayType || mType == kMapType);
    return std::dynamic_pointer_cast<LiveDataObject>(mData);
}

std::shared_ptr<datagrammar::Node>
Object::getNode() const
{
    assert(mType == kNodeType);
    return std::static_pointer_cast<datagrammar::Node>(mData);
}

bool
Object::truthy() const
{
    switch (mType) {
        case kNullType:
            return false;
        case kBoolType:
        case kNumberType:
            return mValue != 0;
        case kStringType:
            return mString.size() != 0;
        case kArrayType:
        case kMapType:
        case kNodeType:
        case kFunctionType:
            return true;
        case kAbsoluteDimensionType:
        case kRelativeDimensionType:
            return mValue != 0;
        case kAutoDimensionType:
        case kColorType:
        case kFilterType:
        case kGradientType:
        case kMediaSourceType:
            return true;
        case kRectType:
            return !mData->getRect().isEmpty();
        case kRadiiType:
            return !mData->getRadii().isEmpty();
        case kStyledTextType:
            return mData->getStyledText().getText().size() != 0 || !mData->getStyledText().getSpans().empty();
        case kGraphicType:
            return true;
        case kTransformType:
            return true;
        case kTransform2DType:
            return true;
        case kEasingType:
            return true;
        case kBoundSymbolType:
            return true;
    }

    // Should never be reached.
    return true;
}

// Methods for MAP objects
const Object
Object::get(const std::string& key) const
{
    assert(mType == kMapType);
    return mData->get(key);
}

bool
Object::has(const std::string& key) const
{
    assert(mType == kMapType);
    return mData->has(key);
}

const Object
Object::opt(const std::string& key, const Object& def) const
{
    assert(mType == kMapType);
    return mData->opt(key, def);
}

// Methods for ARRAY objects
const Object
Object::at(size_t index) const
{
    assert(mType == kArrayType);
    return mData->at(index);
}

const Object::ObjectType
Object::getType() const
{
    return mType;
}


size_t
Object::size() const
{
    switch (mType) {
        case kArrayType:
        case kMapType:
            return mData->size();
        case kStringType:
            return mString.size();
        case kStyledTextType:
            return mData->getStyledText().asString().size();  // Size of the raw text
        default:
            return 0;
    }
}

bool
Object::empty() const
{
    switch (mType) {
        case kNullType:
            return true;
        case kArrayType:
        case kMapType:
        case kRectType:
            return mData->empty();
        case kStringType:
            return mString.empty();
        case kStyledTextType:
            return mData->getStyledText().asString().empty();  // Only true if the raw text is empty
        default:
            return false;
    }
}

bool
Object::isMutable() const
{
    switch (mType) {
        case kArrayType:
        case kMapType:
            return mData->isMutable();
        default:
            return false;
    }
}

Object
Object::eval() const
{
    return (mType == kNodeType || mType == kBoundSymbolType) ? mData->eval() : *this;
}

/**
 * Internal visitor class used to check if an equation is "pure" - that is, if the result
 * of the equation does not change each time you calculate it.
 */
class PureVisitor : public Visitor<Object> {
public:
    void visit(const Object& object) override {
        if (object.isFunction() && !object.getFunction()->isPure())
            mIsPure = false;
    }

    bool isPure() const { return mIsPure; }
    bool isAborted() const override { return !mIsPure; }  // Abort if the visitor has found a non-pure value

private:
    bool mIsPure = true;
};

bool
Object::isPure() const
{
    PureVisitor visitor;
    accept(visitor);
    return visitor.isPure();
}

const bool DEBUG_SYMBOL_VISITOR = false;

/**
 * Internal visitor class used to extract all symbols and symbol paths from within
 * an equation.
 */
class SymbolVisitor : public Visitor<Object> {
public:
    SymbolVisitor(SymbolReferenceMap& map) : mMap(map) {}

    /**
     * Visit an individual object.  At the end of this visit, the mCurrentSuffix
     * should be set to a valid suffix (either a continuation of the parent or empty).
     * @param object The object to visit
     */
    void visit(const Object& object) override {
        LOG_IF(DEBUG_SYMBOL_VISITOR) << object.toDebugString()
                                     << " mParentSuffix=" << mParentSuffix
                                     << " mIndex=" << mIndex;

        mCurrentSuffix.clear();  // In the majority of cases there will be no suffix

        if (object.isBoundSymbol()) {  // A bound symbol should be added to the map with existing suffixes
            auto symbol = object.getBoundSymbol()->getSymbol();
            if (symbol.second)  // An invalid bound symbol will not have a context
                mMap.emplace(symbol.first + (mIndex == 0 ? mParentSuffix : ""), symbol.second);
        }
        else if (object.isNode()) {  // A node may prepend a string to the suffix or reset the suffix to a new value
            auto suffix = object.getNode()->getSuffix();
            if (!suffix.empty())
                mCurrentSuffix = suffix + "/" + (mIndex == 0 ? mParentSuffix : "");
        }

        mIndex++;
    }

    /**
     * Move down to the child nodes below the current node.  Stash information on the
     * stack so we can recover state
     */
    void push() override {
        mStack.push({mIndex, mParentSuffix});
        mParentSuffix = mCurrentSuffix;
        mIndex = 0;
    }

    /**
     * Pop up one level, restoring the state
     */
    void pop() override {
        const auto& ref = mStack.top();
        mIndex = ref.first;
        mParentSuffix = ref.second;
        mStack.pop();
    }

private:
    SymbolReferenceMap& mMap;
    int mIndex = 0;              // The index of the child being visited.
    std::string mParentSuffix;   // The suffix created by the parent of this object
    std::string mCurrentSuffix;  // The suffix calculated visiting the current object
    std::stack<std::pair<int, std::string>> mStack;  // Old indexes and parent suffixes
};

void
Object::symbols(SymbolReferenceMap& symbols) const {
    SymbolVisitor visitor(symbols);
    accept(visitor);
}

Object
Object::call(const ObjectArray& args) const {
    assert(mType == kFunctionType);
    LOG_IF(OBJECT_DEBUG) << "Calling user function";
    return mData->call(args);
}

// Visitor pattern
void
Object::accept(Visitor<Object>& visitor) const
{
    visitor.visit(*this);
    if (!visitor.isAborted() && (mType == kArrayType || mType == kMapType || mType == kNodeType))
        mData->accept(visitor);
}

rapidjson::Value
Object::serialize(rapidjson::Document::AllocatorType& allocator) const
{
    switch (mType) {
        case kNullType:
            return rapidjson::Value();
        case kBoolType:
            return rapidjson::Value(static_cast<bool>(mValue));
        case kNumberType:
            return rapidjson::Value(mValue);
        case kStringType:
            return rapidjson::Value(mString.c_str(), allocator);
        case kArrayType: {
            rapidjson::Value v(rapidjson::kArrayType);
            for (int i = 0 ; i < size() ; i++)
                v.PushBack(at(i).serialize(allocator), allocator);
            return v;
        }
        case kMapType: {
            rapidjson::Value m(rapidjson::kObjectType);
            for (auto &kv : mData->getMap())
                m.AddMember(rapidjson::Value(kv.first.c_str(), allocator), kv.second.serialize(allocator).Move(), allocator);
            return m;
        }
        case kNodeType:
            return rapidjson::Value("UNABLE TO SERIALIZE NODE", allocator);
        case kFunctionType:
            return rapidjson::Value("UNABLE TO SERIALIZE FUNCTION", allocator);
        case kAbsoluteDimensionType:
            return rapidjson::Value(mValue);
        case kRelativeDimensionType:
            return rapidjson::Value((doubleToString(mValue)+"%").c_str(), allocator);
        case kAutoDimensionType:
            return rapidjson::Value("auto", allocator);
        case kColorType:
            return rapidjson::Value(asString().c_str(), allocator);
        case kFilterType:
            return getFilter().serialize(allocator);
        case kGradientType:
            return getGradient().serialize(allocator);
        case kMediaSourceType:
            return getMediaSource().serialize(allocator);
        case kRectType:
            return getRect().serialize(allocator);
        case kRadiiType:
            return getRadii().serialize(allocator);
        case kStyledTextType:
            return getStyledText().serialize(allocator);
        case kGraphicType:
            return getGraphic()->serialize(allocator);
        case kTransformType:
            return rapidjson::Value("UNABLE TO SERIALIZE TRANSFORM", allocator);
        case kTransform2DType:
            return getTransform2D().serialize(allocator);
        case kEasingType:
            return rapidjson::Value("UNABLE TO SERIALIZE EASING", allocator);
        case kBoundSymbolType:
            return rapidjson::Value("UNABLE TO SERIALIZE BOUND SYMBOL", allocator);
    }

    return rapidjson::Value();  // Never should be reached
}

rapidjson::Value
Object::serializeDirty(rapidjson::Document::AllocatorType& allocator) const
{
    switch (mType) {
        case kGraphicType:
            // TODO: Fix this - should return just the dirty bits
            return serialize(allocator);
            break;
        default:
            return serialize(allocator);
    }
}

std::string
Object::toDebugString() const
{
    switch (mType) {
        case Object::kNullType:
            return "null";
        case Object::kBoolType:
            return (mValue ? "true" : "false");
        case Object::kNumberType:
            return std::to_string(mValue);
        case Object::kStringType:
            return "'" + mString + "'";
        case Object::kMapType:
        case Object::kArrayType:
        case Object::kNodeType:
        case Object::kFunctionType:
            return mData->toDebugString();
        case Object::kAbsoluteDimensionType:
            return "AbsDim<" + std::to_string(mValue) + ">";
        case Object::kRelativeDimensionType:
            return "RelDim<" + std::to_string(mValue) + ">";
        case Object::kAutoDimensionType:
            return "AutoDim";
        case Object::kColorType:
            return asString();
        case Object::kFilterType:
        case Object::kGradientType:
        case Object::kMediaSourceType:
        case Object::kRectType:
        case Object::kRadiiType:
        case Object::kStyledTextType:
        case Object::kGraphicType:
        case Object::kTransformType:
        case Object::kTransform2DType:
        case Object::kEasingType:
        case Object::kBoundSymbolType:
            return mData->toDebugString();
    }
}

streamer&
operator<<(streamer& os, const Object& object)
{
    os << object.toDebugString();
    return os;
}

}  // namespace apl
