// Stub fbxsdk.h for compilation without the real Autodesk FBX SDK 2015.1
// Provides minimal type stubs to allow the studiomdl code to compile.
// The resulting binary will NOT have functional FBX import without libfbxsdk.dll.

#pragma once

#ifndef _FBXSDK_H_
#define _FBXSDK_H_

#include <string.h>
#include <stddef.h>
#include <string>

// Suppress MSVC warnings for stub implementations
#pragma warning(push)
#pragma warning(disable: 4100 4127 4189 4505 4702)

// Version info
#define FBXSDK_VERSION_MAJOR 2015
#define FBXSDK_VERSION_MINOR 1
#define FBXSDK_VERSION_POINT 0
#define FBXSDK_VERSION "2015.1"

// Macros
#define FBXSDK_CRT_SECURE_NO_WARNING_BEGIN
#define FBXSDK_CRT_SECURE_NO_WARNING_END
#define FBXSDK_NEW_API
#define IOSROOT "IOSRoot"

// IOSettings property name constants
#define IMP_FBX_MATERIAL        "IMP|FBX|Material"
#define IMP_FBX_TEXTURE         "IMP|FBX|Texture"
#define IMP_FBX_LINK            "IMP|FBX|Link"
#define IMP_FBX_SHAPE           "IMP|FBX|Shape"
#define IMP_FBX_GOBO            "IMP|FBX|Gobo"
#define IMP_FBX_ANIMATION       "IMP|FBX|Animation"
#define IMP_FBX_GLOBAL_SETTINGS "IMP|FBX|Global_Settings"
#define IMP_FBX_PASSWORD        "IMP|FBX|Password|file"
#define IMP_FBX_PASSWORD_ENABLE "IMP|FBX|Password|enable"

// ---- Primitive typedefs ----
typedef double      FbxDouble;
typedef float       FbxFloat;
typedef int         FbxInt;
typedef long long   FbxLongLong;

// FBX type enum (used by FbxDataType::GetType())
enum EFbxType {
    eFbxUndefined = 0,
    eFbxChar,
    eFbxUChar,
    eFbxShort,
    eFbxUShort,
    eFbxInt,
    eFbxUInt,
    eFbxLongLong,
    eFbxULongLong,
    eFbxFloat,
    eFbxDouble,
    eFbxDouble2,
    eFbxDouble3,
    eFbxDouble4,
    eFbxDouble4x4,
    eFbxEnum,
    eFbxString,
    eFbxTime,
    eFbxReference,
    eFbxBlob,
    eFbxDistance,
    eFbxDateTime,
    eFbxTypeCount
};

// ---- FbxString ----
class FbxString
{
public:
    FbxString() { m_buf[0]=0; }
    FbxString(const char* s) { if(s) strncpy_s(m_buf,2047,s,2047); else m_buf[0]=0; m_buf[2047]=0; }
    FbxString(const FbxString& o) { memcpy(m_buf,o.m_buf,2048); }
    FbxString& operator=(const FbxString& o) { memcpy(m_buf,o.m_buf,2048); return *this; }
    FbxString& operator=(const char* s) { if(s) strncpy_s(m_buf,2047,s,2047); else m_buf[0]=0; m_buf[2047]=0; return *this; }
    FbxString& operator+=(const char* s) {
        size_t l=strlen(m_buf);
        if(s) strncat_s(m_buf+l, 2047-l, s, 2047-l);
        return *this;
    }
    FbxString& operator+=(const FbxString& o) { return operator+=(o.m_buf); }
    bool operator==(const FbxString& o) const { return strcmp(m_buf,o.m_buf)==0; }
    bool operator!=(const FbxString& o) const { return !operator==(o); }
    const char* Buffer() const { return m_buf; }
    operator const char*() const { return m_buf; }
    bool IsEmpty() const { return m_buf[0]==0; }
    FbxString operator+(const char* s) const { FbxString r(*this); r+=s; return r; }
    int GetLen() const { return (int)strlen(m_buf); }
    // FindAndReplace: replace first occurrence of pFind starting at startPos with pReplace
    // Returns true if found and replaced
    bool FindAndReplace(const char* pFind, const char* pReplace, int startPos=0) {
        if(!pFind || !pReplace) return false;
        const char* found = strstr(m_buf + startPos, pFind);
        if(!found) return false;
        char tmp[2048];
        int prefixLen = (int)(found - m_buf);
        int findLen = (int)strlen(pFind);
        int replaceLen = (int)strlen(pReplace);
        strncpy_s(tmp, 2047, m_buf, prefixLen);
        tmp[prefixLen] = 0;
        strncat_s(tmp, 2047, pReplace, replaceLen);
        strncat_s(tmp, 2047, found + findLen, 2047);
        tmp[2047] = 0;
        memcpy(m_buf, tmp, 2048);
        return true;
    }
private:
    char m_buf[2048];
};

// ---- FbxVector2 ----
class FbxVector2
{
public:
    FbxVector2() { mData[0]=mData[1]=0; }
    FbxVector2(double x, double y) { mData[0]=x; mData[1]=y; }
    double& operator[](int i) { return mData[i]; }
    const double& operator[](int i) const { return mData[i]; }
    double mData[2];
};

// ---- FbxVector4 ----
class FbxVector4
{
public:
    FbxVector4() { mData[0]=mData[1]=mData[2]=0; mData[3]=1; }
    FbxVector4(double x, double y, double z, double w=1.0) { mData[0]=x; mData[1]=y; mData[2]=z; mData[3]=w; }
    double& operator[](int i) { return mData[i]; }
    const double& operator[](int i) const { return mData[i]; }
    FbxVector4 DecomposeSphericalXYZ() const { return FbxVector4(0,0,0,1); }
    void Normalize() { /* stub */ }
    double mData[4];
};

// ---- FbxQuaternion ----
class FbxQuaternion
{
public:
    FbxQuaternion() { mData[0]=mData[1]=mData[2]=0; mData[3]=1; }
    FbxQuaternion(double x, double y, double z, double w) { mData[0]=x; mData[1]=y; mData[2]=z; mData[3]=w; }
    double& operator[](int i) { return mData[i]; }
    const double& operator[](int i) const { return mData[i]; }
    FbxVector4 DecomposeSphericalXYZ() const { return FbxVector4(0,0,0,1); }
    double mData[4];
};

// ---- FbxMatrix ----
class FbxMatrix
{
public:
    FbxMatrix() { memset(mData, 0, sizeof(mData)); for(int i=0;i<4;i++) mData[i][i]=1.0; }
    // Allow construction from array row (for operator[])
    double* operator[](int r) { return mData[r]; }
    const double* operator[](int r) const { return mData[r]; }
    double& operator()(int r, int c) { return mData[r][c]; }
    void GetElements(FbxVector4& t, FbxQuaternion& r, FbxVector4& sh, FbxVector4& sc, double& sign) const {}
    FbxMatrix Inverse() const { return FbxMatrix(); }
    FbxMatrix Transpose() const {
        FbxMatrix m;
        for(int r=0;r<4;r++) for(int c=0;c<4;c++) m.mData[r][c]=mData[c][r];
        return m;
    }
    FbxMatrix operator*(const FbxMatrix& o) const {
        FbxMatrix r;
        for(int i=0;i<4;i++) {
            for(int j=0;j<4;j++) {
                r.mData[i][j]=0;
                for(int k=0;k<4;k++) r.mData[i][j]+=mData[i][k]*o.mData[k][j];
            }
        }
        return r;
    }
    bool operator==(const FbxMatrix& o) const {
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) if(mData[i][j]!=o.mData[i][j]) return false;
        return true;
    }
    bool operator!=(const FbxMatrix& o) const { return !operator==(o); }
    // MultNormalize: transform vector by matrix, normalize result
    FbxVector4 MultNormalize(const FbxVector4& v) const {
        double x = mData[0][0]*v[0]+mData[0][1]*v[1]+mData[0][2]*v[2]+mData[0][3]*v[3];
        double y = mData[1][0]*v[0]+mData[1][1]*v[1]+mData[1][2]*v[2]+mData[1][3]*v[3];
        double z = mData[2][0]*v[0]+mData[2][1]*v[1]+mData[2][2]*v[2]+mData[2][3]*v[3];
        double w = mData[3][0]*v[0]+mData[3][1]*v[1]+mData[3][2]*v[2]+mData[3][3]*v[3];
        return FbxVector4(x,y,z,w);
    }
    double mData[4][4];
};

// ---- FbxAMatrix ----
class FbxAMatrix
{
public:
    FbxAMatrix() { memset(mData, 0, sizeof(mData)); for(int i=0;i<4;i++) mData[i][i]=1.0; }
    FbxVector4 GetT() const { return FbxVector4(); }
    FbxVector4 GetR() const { return FbxVector4(); }
    FbxVector4 GetS() const { return FbxVector4(1,1,1,1); }
    FbxQuaternion GetQ() const { return FbxQuaternion(0,0,0,1); }
    FbxAMatrix Inverse() const { return FbxAMatrix(); }
    // Implicit conversion to FbxMatrix
    operator FbxMatrix() const {
        FbxMatrix m;
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) m.mData[i][j]=mData[i][j];
        return m;
    }
    double mData[4][4];
};

// ---- FbxColor ----
class FbxColor
{
public:
    FbxColor() : mRed(0),mGreen(0),mBlue(0),mAlpha(1) {}
    FbxColor(double r, double g, double b, double a=1.0) : mRed(r),mGreen(g),mBlue(b),mAlpha(a) {}
    // operator[] for channel access: 0=R, 1=G, 2=B, 3=A
    double operator[](int i) const {
        switch(i) { case 0: return mRed; case 1: return mGreen; case 2: return mBlue; default: return mAlpha; }
    }
    double& operator[](int i) {
        switch(i) { case 0: return mRed; case 1: return mGreen; case 2: return mBlue; default: return mAlpha; }
    }
    double mRed, mGreen, mBlue, mAlpha;
};

// ---- FbxTime ----
class FbxTime
{
public:
    enum EMode {
        eDefaultMode=0, eFrames120=1, eFrames100=2, eFrames60=3, eFrames50=4, eFrames48=5,
        eFrames30=6, eFrames30Drop=7, eNTSCDropFrame=8, eNTSCFullFrame=9, ePAL=10,
        eFrames24=11, eFrames1000=12, eFilmFullFrame=13, eCustom=14, eFrames96=15,
        eFrames72=16, eFrames59dot94=17, eFrames119dot88=18
    };
    FbxTime() : m_time(0) {}
    FbxTime(long long t) : m_time(t) {}
    long long GetMilliSeconds() const { return m_time; }
    double GetSecondDouble() const { return (double)m_time / 1000.0; }
    double GetFrameCountPrecise(EMode mode=eDefaultMode) const { return 0.0; }
    static double GetFrameRate(EMode mode) {
        switch(mode) {
            case eFrames30: return 30.0;
            case eFrames24: return 24.0;
            case eFrames60: return 60.0;
            case eFrames48: return 48.0;
            case eFrames50: return 50.0;
            default: return 30.0;
        }
    }
    FbxLongLong GetFrameCount(EMode mode=eDefaultMode) const { return (FbxLongLong)(m_time / 1000 * 30 / 1000); }
    void SetFrame(FbxLongLong frame, EMode mode=eDefaultMode) { m_time = frame * 1000 / 30 * 1000; }
    FbxTime operator+(const FbxTime& o) const { return FbxTime(m_time + o.m_time); }
    FbxTime operator-(const FbxTime& o) const { return FbxTime(m_time - o.m_time); }
    bool operator<(const FbxTime& o) const { return m_time < o.m_time; }
    bool operator<=(const FbxTime& o) const { return m_time <= o.m_time; }
    bool operator>(const FbxTime& o) const { return m_time > o.m_time; }
    bool operator>=(const FbxTime& o) const { return m_time >= o.m_time; }
    FbxTime& operator+=(const FbxTime& o) { m_time += o.m_time; return *this; }
private:
    long long m_time;
};

typedef FbxTime::EMode FbxTimeMode;

// ---- FbxDataType ----
class FbxDataType
{
public:
    FbxDataType() {}
    const char* GetName() const { return ""; }
    bool Is(const FbxDataType& other) const { return false; }
    EFbxType GetType() const { return eFbxUndefined; }
};

// ---- FbxPropertyFlags ----
struct FbxPropertyFlags
{
    enum EFlags { eUserDefined = (1<<4) };
};

// Forward declare FbxObject so FbxProperty can reference it
class FbxObject;

// ---- FbxProperty ----
// Must be defined before FbxObject's full definition
class FbxProperty
{
public:
    FbxProperty() {}
    bool IsValid() const { return false; }
    FbxDataType GetPropertyDataType() const { return FbxDataType(); }
    const char* GetName() const { return ""; }
    const char* GetNameAsCStr() const { return ""; }
    FbxProperty GetNextProperty() const { return FbxProperty(); }
    FbxProperty GetNextDescendent(const FbxProperty& p) const { return FbxProperty(); }
    FbxProperty GetFirstDescendent() const { return FbxProperty(); }
    bool GetFlag(int f) const { return false; }
    const char* GetLabel(bool pLocalize=false) const { return ""; }
    int GetSrcObjectCount() const { return 0; }
    int GetSrcObjectCount(int classId) const { return 0; }
    void* GetSrcObjectPtr(int idx) const { return nullptr; }
    template<class T> T* GetSrcObject(int idx=0) const { return nullptr; }
    // GetSrcObject(classId, idx) - returns FbxObject*
    FbxObject* GetSrcObject(int classId, int idx) const { return nullptr; }

    // Typed value accessor - returns default-constructed T
    template<class T> T Get() const { return T(); }

    // For FbxString properties (.Get() used to get string value)
    FbxString Get() const { return FbxString(""); }

    // Limits
    bool HasMinLimit() const { return false; }
    bool HasMaxLimit() const { return false; }
    double GetMinLimit() const { return 0.0; }
    double GetMaxLimit() const { return 1.0; }

    // EvaluateValue<T>(time) - evaluate animated property at a given time
    template<class T> T EvaluateValue(FbxTime time) const { return T(); }
};

// ---- FbxStatus ----
class FbxStatus
{
public:
    enum EStatusCode { eSuccess=0, eInvalidFileVersion=1, ePasswordError=2 };
    EStatusCode GetCode() const { return eSuccess; }
    const char* GetErrorString() const { return ""; }
};

// ---- FbxArray ----
template<typename T>
class FbxArray
{
public:
    int GetCount() const { return 0; }
    T GetAt(int idx) const { return T(); }
    T operator[](int idx) const { return T(); }
    T& operator[](int idx) { static T dummy; return dummy; }
    void Add(T item) {}
    void Clear() {}
};

// ---- FbxMap ----
// Needs to support Iterator pattern: Begin(), End(), Iterator->GetKey(), Iterator->GetValue()
template<typename K, typename V, typename Cmp=void>
class FbxMap
{
public:
    struct Record {
        K key; V value;
        const K& GetKey() const { return key; }
        V& GetValue() { return value; }
        const V& GetValue() const { return value; }
    };
    typedef Record* Iterator;
    bool Empty() const { return true; }
    int GetSize() const { return 0; }
    void Insert(const K& k, const V& v) {}
    Iterator Begin() { return nullptr; }
    Iterator End() { return nullptr; }
};

// ---- FbxObject (base) ----
class FbxManager;
class FbxObject
{
public:
    virtual ~FbxObject() {}
    virtual void Destroy() {}
    const char* GetName() const { return ""; }
    const char* GetNameOnly() const { return ""; }
    void SetName(const char* name) {}
    FbxManager* GetFbxManager() const { return nullptr; }
    const char* GetTypeName() const { return "FbxObject"; }
    int GetDstObjectCount() const { return 0; }
    int GetDstObjectCount(int classId) const { return 0; }
    FbxObject* GetDstObject(int idx) const { return nullptr; }
    FbxProperty GetFirstProperty() const { return FbxProperty(); }
    FbxProperty GetNextProperty(const FbxProperty& p) const { return FbxProperty(); }
    FbxProperty FindProperty(const char* name, bool caseSensitive=true) const { return FbxProperty(); }
    int GetSrcObjectCount() const { return 0; }
    int GetSrcObjectCount(int classId) const { return 0; }
    FbxObject* GetSrcObject(int idx) const { return nullptr; }
    FbxObject* GetSrcObject(int classId, int idx) const { return nullptr; }
    // Template versions for typed access
    template<class T> T* GetSrcObject(int idx=0) const { return nullptr; }
    template<class T> T* GetDstObject(int idx=0) const { return nullptr; }
    template<class T> int GetSrcObjectCount() const { return 0; }
};

// ---- FbxAxisSystem ----
class FbxScene; // forward
class FbxAxisSystem
{
public:
    enum EUpVector    { eXAxis = 1, eYAxis = 2, eZAxis = 3 };
    enum EFrontVector { eParityEven = 1, eParityOdd = 2 };
    enum ECoordSystem { eRightHanded = 0, eLeftHanded = 1 };

    struct AxisDef { int mAxis; int mSign; };
    AxisDef mUpVector;
    AxisDef mFrontVector;
    AxisDef mCoorSystem;

    FbxAxisSystem() { mUpVector={2,1}; mFrontVector={1,1}; mCoorSystem={0,1}; }
    FbxAxisSystem(EUpVector u, EFrontVector f, ECoordSystem c)
    {
        mUpVector.mAxis = (int)u; mUpVector.mSign = 1;
        mFrontVector.mAxis = (int)f; mFrontVector.mSign = 1;
        mCoorSystem.mAxis = (int)c; mCoorSystem.mSign = 1;
    }

    FbxAxisSystem& operator=(const FbxAxisSystem& rhs)
    {
        mUpVector = rhs.mUpVector;
        mFrontVector = rhs.mFrontVector;
        mCoorSystem = rhs.mCoorSystem;
        return *this;
    }

    int GetUpVector(int& sign) const { sign=mUpVector.mSign; return mUpVector.mAxis; }
    int GetFrontVector(int& sign) const { sign=mFrontVector.mSign; return mFrontVector.mAxis; }
    int GetCoorSystem() const { return mCoorSystem.mAxis; }

    void ConvertScene(FbxScene* pScene) const {}
    void GetConversionMatrix(const FbxAxisSystem& from, FbxMatrix& mat) const {}
};

// ---- FbxSystemUnit ----
class FbxSystemUnit
{
public:
    FbxSystemUnit() {}
    FbxSystemUnit(double scale) {}
    void ConvertScene(FbxScene* pScene) const {}
};

// ---- FbxIOSettings ----
class FbxIOSettings : public FbxObject
{
public:
    static FbxIOSettings* Create(FbxManager* pMgr, const char* name) { return nullptr; }
    void SetBoolProp(const char* name, bool val) {}
    void SetStringProp(const char* name, const FbxString& val) {}
    void SetIntProp(const char* name, int val) {}
    bool GetBoolProp(const char* name, bool defVal=false) const { return defVal; }
};

// ---- FbxIOBase ----
class FbxIOBase : public FbxObject
{
public:
    enum EError { ePasswordError = 2 };
};

// ---- Forward declarations for classes used in methods ----
class FbxDeformer;
class FbxNode;
class FbxGeometryElementNormal;
class FbxGeometryElementUV;
class FbxGeometryElementVertexColor;
class FbxGeometryElementMaterial;
class FbxGeometryElementUserData;
class FbxAnimLayer;
class FbxAnimCurve;
class FbxSkin;
class FbxBlendShape;
class FbxBlendShapeChannel;
class FbxShape;
class FbxCluster;
class FbxSurfaceMaterial;
class FbxFileTexture;
class FbxDocumentInfo;
class FbxAnimStack;
class FbxTakeInfo;

// ---- FbxDeformer ----
class FbxDeformer : public FbxObject
{
public:
    enum EDeformerType { eSkin=0, eBlendShape=1 };
    EDeformerType GetDeformerType() const { return eSkin; }
};

// ---- FbxLayerElement ----
class FbxLayerElement
{
public:
    enum EMappingMode { eNone=0, eByControlPoint=1, eByPolygonVertex=2, eByPolygon=3, eAllSame=4, eByEdge=5 };
    enum EReferenceMode { eDirect=0, eIndex=1, eIndexToDirect=2 };
    EMappingMode GetMappingMode() const { return eByPolygonVertex; }
    EReferenceMode GetReferenceMode() const { return eDirect; }
};

// ---- FbxLayerElementArray ----
class FbxLayerElementArray
{
public:
    enum ELockType { eReadLock=1, eWriteLock=2, eReadWriteLock=3 };
    void ReadLock() const {}
    void ReadUnLock() const {}
    int GetCount() const { return 0; }
    void* GetLocked(ELockType type=eReadLock) { return nullptr; }
    // Overload used in dmfbxserializer: GetLocked(T*& ptr, ELockType) - sets the pointer
    template<typename T>
    T* GetLocked(T*& outPtr, ELockType type=eReadLock) { outPtr = nullptr; return nullptr; }
    void Release(void** buf) { if(buf) *buf=nullptr; }
};

// ---- FbxLayerElementArrayTemplate ----
template<typename T>
class FbxLayerElementArrayTemplate : public FbxLayerElementArray
{
public:
    T GetAt(int idx) const { return T(); }
    T& operator[](int idx) { static T dummy; return dummy; }
    const T& operator[](int idx) const { static T dummy; return dummy; }
    int GetCount() const { return 0; }
};

// ---- FbxGeometryElement (base for typed geometry elements) ----
class FbxGeometryElement : public FbxLayerElement
{
public:
    // Re-expose enum values so FbxGeometryElement::eByControlPoint etc. work
    using FbxLayerElement::EMappingMode;
    using FbxLayerElement::EReferenceMode;
    using FbxLayerElement::eNone;
    using FbxLayerElement::eByControlPoint;
    using FbxLayerElement::eByPolygonVertex;
    using FbxLayerElement::eByPolygon;
    using FbxLayerElement::eAllSame;
    using FbxLayerElement::eByEdge;
    using FbxLayerElement::eDirect;
    using FbxLayerElement::eIndex;
    using FbxLayerElement::eIndexToDirect;

    FbxLayerElementArrayTemplate<int>& GetIndexArray() { static FbxLayerElementArrayTemplate<int> a; return a; }
    const char* GetName() const { return ""; }
};

// ---- FbxGeometryElementNormal ----
class FbxGeometryElementNormal : public FbxGeometryElement
{
public:
    FbxLayerElementArrayTemplate<FbxVector4>& GetDirectArray() { static FbxLayerElementArrayTemplate<FbxVector4> a; return a; }
    const FbxLayerElementArrayTemplate<FbxVector4>& GetDirectArray() const { static FbxLayerElementArrayTemplate<FbxVector4> a; return a; }
    FbxLayerElementArrayTemplate<int>& GetIndexArray() { static FbxLayerElementArrayTemplate<int> a; return a; }
};

// ---- FbxGeometryElementUV ----
class FbxGeometryElementUV : public FbxGeometryElement
{
public:
    FbxLayerElementArrayTemplate<FbxVector2>& GetDirectArray() { static FbxLayerElementArrayTemplate<FbxVector2> a; return a; }
    const FbxLayerElementArrayTemplate<FbxVector2>& GetDirectArray() const { static FbxLayerElementArrayTemplate<FbxVector2> a; return a; }
    FbxLayerElementArrayTemplate<int>& GetIndexArray() { static FbxLayerElementArrayTemplate<int> a; return a; }
    const char* GetName() const { return ""; }
};

// ---- FbxGeometryElementVertexColor ----
class FbxGeometryElementVertexColor : public FbxGeometryElement
{
public:
    FbxLayerElementArrayTemplate<FbxColor>& GetDirectArray() { static FbxLayerElementArrayTemplate<FbxColor> a; return a; }
    const FbxLayerElementArrayTemplate<FbxColor>& GetDirectArray() const { static FbxLayerElementArrayTemplate<FbxColor> a; return a; }
    FbxLayerElementArrayTemplate<int>& GetIndexArray() { static FbxLayerElementArrayTemplate<int> a; return a; }
    const char* GetName() const { return ""; }
};

// ---- FbxGeometryElementMaterial ----
class FbxGeometryElementMaterial : public FbxGeometryElement
{
public:
    FbxLayerElementArrayTemplate<int>& GetIndexArray() { static FbxLayerElementArrayTemplate<int> a; return a; }
};

// ---- FbxGeometryElementUserData ----
class FbxGeometryElementUserData : public FbxGeometryElement
{
public:
    // GetDataType(j) returns the data type of the j-th field
    FbxDataType GetDataType(int idx=0) const { return FbxDataType(); }
    const char* GetDataName(int idx=0) const { return ""; }
    int GetDataCount() const { return 0; }
    // GetDirectArrayCount: number of arrays (fields) in this user data element
    int GetDirectArrayCount() const { return 0; }
    void* GetData(int elemIdx, int dataIdx) const { return nullptr; }
    int GetArrayCount() const { return 0; }
    // GetDirectArrayVoid(fieldIdx, &bIsArray): returns typed array for the field
    FbxLayerElementArrayTemplate<void*>* GetDirectArrayVoid(int fieldIdx, bool* pIsArray=nullptr) {
        if(pIsArray) *pIsArray = false;
        return nullptr;
    }
    FbxLayerElementArrayTemplate<int>& GetIndexArray() { static FbxLayerElementArrayTemplate<int> a; return a; }
};

// ---- FbxNodeAttribute ----
// In the real FBX SDK, FbxGeometry also inherits FbxNodeAttribute.
// We define FbxNodeAttribute here so FbxGeometry can inherit it.
class FbxNodeAttribute : public FbxObject
{
public:
    enum EType { eUnknown=0, eNull=1, eMarker=2, eSkeleton=3, eMesh=4, eNurbs=5, ePatch=6,
                 eCamera=7, eCameraSwitch=8, eLight=9, eOpticalReference=10 };
    virtual EType GetAttributeType() const { return eUnknown; }
};

// ---- FbxGeometry ----
// FbxGeometry inherits FbxNodeAttribute in the real SDK (FbxMesh is both geometry and node attribute)
class FbxGeometry : public FbxNodeAttribute
{
public:
    int GetDeformerCount(FbxDeformer::EDeformerType type) const { return 0; }
    FbxDeformer* GetDeformer(int idx, FbxDeformer::EDeformerType type) const { return nullptr; }
    FbxAnimCurve* GetShapeChannel(int blendShapeIdx, int channelIdx, FbxAnimLayer* pLayer, bool create=false) const { return nullptr; }
};

// ---- FbxShape ----
class FbxShape : public FbxGeometry
{
public:
    int GetControlPointsCount() const { return 0; }
    FbxVector4* GetControlPoints() const { return nullptr; }
    FbxGeometryElementNormal* GetElementNormal(int idx=0) const { return nullptr; }
    int GetElementNormalCount() const { return 0; }
    int GetControlPointIndicesCount() const { return 0; }
    int* GetControlPointIndices() const { return nullptr; }
};

// ---- FbxMesh ----
class FbxMesh : public FbxGeometry
{
public:
    int GetControlPointsCount() const { return 0; }
    FbxVector4* GetControlPoints() const { return nullptr; }
    int GetPolygonCount() const { return 0; }
    int GetPolygonSize(int polyIdx) const { return 0; }
    int GetPolygonVertex(int polyIdx, int vertIdx) const { return 0; }
    FbxGeometryElementNormal* GetElementNormal(int idx=0) const { return nullptr; }
    int GetElementNormalCount() const { return 0; }
    FbxGeometryElementUV* GetElementUV(int idx=0) const { return nullptr; }
    FbxGeometryElementUV* GetElementUV(const char* name) const { return nullptr; }
    int GetElementUVCount() const { return 0; }
    FbxGeometryElementVertexColor* GetElementVertexColor(int idx=0) const { return nullptr; }
    int GetElementVertexColorCount() const { return 0; }
    FbxGeometryElementMaterial* GetElementMaterial(int idx=0) const { return nullptr; }
    int GetElementMaterialCount() const { return 0; }
    FbxGeometryElementUserData* GetElementUserData(int idx=0) const { return nullptr; }
    int GetElementUserDataCount() const { return 0; }
    bool GetPolygonVertexNormal(int polyIdx, int vertIdx, FbxVector4& normal) const { return false; }
    bool GetPolygonVertexUV(int polyIdx, int vertIdx, const char* uvSetName, FbxVector2& uv, bool& unmapped) const { return false; }
    int GetElementPolygonGroupCount() const { return 0; }
    void ComputeVertexNormals(bool pCW=false) {}
    int GetUVSetCount() const { return 0; }
    void GetUVSetNames(FbxArray<FbxString*>& names) const {}
    FbxNode* GetNode() const { return nullptr; }
};

// ---- FbxSkin ----
class FbxSkin : public FbxDeformer
{
public:
    int GetClusterCount() const { return 0; }
    FbxCluster* GetCluster(int idx) const { return nullptr; }
};

// ---- FbxCluster ----
class FbxCluster : public FbxObject
{
public:
    static const int ClassId = 0;
    enum ELinkMode { eNormalize=0, eAdditive=1, eTotalOne=2 };
    ELinkMode GetLinkMode() const { return eNormalize; }
    FbxNode* GetLink() const { return nullptr; }
    int GetControlPointIndicesCount() const { return 0; }
    int* GetControlPointIndices() const { return nullptr; }
    double* GetControlPointWeights() const { return nullptr; }
    void GetTransformMatrix(FbxAMatrix& mat) const {}
    void GetTransformLinkMatrix(FbxAMatrix& mat) const {}
};

// ---- FbxBlendShapeChannel ----
class FbxBlendShapeChannel : public FbxObject
{
public:
    int GetTargetShapeCount() const { return 0; }
    FbxShape* GetTargetShape(int idx) const { return nullptr; }
    double GetTargetShapeFullWeights(int idx) const { return 100.0; }
    double GetDeformPercent() const { return 0.0; }
};

// ---- FbxBlendShape ----
class FbxBlendShape : public FbxDeformer
{
public:
    int GetBlendShapeChannelCount() const { return 0; }
    FbxBlendShapeChannel* GetBlendShapeChannel(int idx) const { return nullptr; }
};

// ---- FbxSurfaceMaterial ----
class FbxSurfaceMaterial : public FbxObject
{
public:
    static const char* sDiffuse;
    static const char* sNormalMap;
    FbxProperty FindProperty(const char* name, bool caseSensitive=true) const { return FbxProperty(); }
    FbxProperty GetFirstProperty() const { return FbxProperty(); }
    FbxProperty GetNextProperty(const FbxProperty& p) const { return FbxProperty(); }
};

// ---- FbxTexture ----
class FbxTexture : public FbxObject
{
public:
    static const int ClassId = 1;
};

// ---- FbxFileTexture ----
class FbxFileTexture : public FbxTexture
{
public:
    const char* GetFileName() const { return ""; }
    const char* GetRelativeFileName() const { return ""; }
};

// ---- FbxLayeredTexture ----
class FbxLayeredTexture : public FbxTexture
{
public:
    static const int ClassId = 2;
};

// ---- FbxNode ----
class FbxNode : public FbxObject
{
public:
    int GetChildCount(bool recursive=false) const { return 0; }
    FbxNode* GetChild(int idx) const { return nullptr; }
    FbxNode* GetParent() const { return nullptr; }
    FbxNodeAttribute* GetNodeAttribute() const { return nullptr; }
    FbxNodeAttribute* GetNodeAttributeByIndex(int idx) const { return nullptr; }
    int GetNodeAttributeCount() const { return 0; }
    FbxMesh* GetMesh() const { return nullptr; }
    int GetMaterialCount() const { return 0; }
    FbxSurfaceMaterial* GetMaterial(int idx) const { return nullptr; }
    FbxAMatrix EvaluateGlobalTransform(void* time=nullptr) const { return FbxAMatrix(); }
    FbxAMatrix EvaluateLocalTransform(void* time=nullptr) const { return FbxAMatrix(); }
    FbxAMatrix EvaluateGlobalTransform(FbxTime t) const { return FbxAMatrix(); }
    FbxAMatrix EvaluateLocalTransform(FbxTime t) const { return FbxAMatrix(); }
    FbxVector4 GetGeometricTranslation(int pNode) const { return FbxVector4(); }
    FbxVector4 GetGeometricRotation(int pNode) const { return FbxVector4(); }
    FbxVector4 GetGeometricScaling(int pNode) const { return FbxVector4(1,1,1,0); }
    FbxProperty GetFirstProperty() const { return FbxProperty(); }
    FbxProperty GetNextProperty(const FbxProperty& p) const { return FbxProperty(); }
    FbxProperty FindProperty(const char* name, bool caseSensitive=true) const { return FbxProperty(); }
    // GetDstObjectCount with classId
    int GetDstObjectCount(int classId) const { return 0; }
    enum EPivotSet { eSourcePivot=0, eDestinationPivot=1 };
};

// ---- FbxAnimCurve ----
class FbxAnimCurve : public FbxObject
{
public:
    int KeyGetCount() const { return 0; }
    FbxTime KeyGetTime(int idx) const { return FbxTime(); }
    float KeyGetValue(int idx) const { return 0.0f; }
    float Evaluate(FbxTime time) const { return 0.0f; }
};

// ---- FbxAnimLayer ----
class FbxAnimLayer : public FbxObject
{
public:
    FbxAnimCurve* GetCurve(FbxNode* node, const char* channel) const { return nullptr; }
    FbxAnimCurve* GetCurve(FbxNode* node, const char* channel, bool create) const { return nullptr; }
    FbxAnimCurve* GetCurve(FbxNode* node, const char* channel, const char* subprop, bool create) const { return nullptr; }
    template<class T> T* GetMember(int idx=0) const { return nullptr; }
};

// ---- FbxAnimStack ----
class FbxAnimStack : public FbxObject
{
public:
    int GetMemberCount() const { return 0; }
    FbxAnimLayer* GetMember(int idx) const { return nullptr; }
    template<class T> T* GetMember(int idx=0) const { return nullptr; }
    FbxString GetName() const { return FbxString(""); }
    FbxTime GetLocalTimeSpanStart() const { return FbxTime(); }
    FbxTime GetLocalTimeSpanStop() const { return FbxTime(); }
    struct FbxTimeSpan { FbxTime mStart; FbxTime mStop; };
    FbxTimeSpan GetLocalTimeSpan() const { FbxTimeSpan s; return s; }
};

// ---- FbxTakeInfo ----
class FbxTakeInfo
{
public:
    FbxString mName;
    FbxString mImportName;
    FbxString mDescription;
    bool mSelect = false;

    // Time span of the take
    struct FbxTimeSpan {
        FbxTime mStart;
        FbxTime mStop;
        FbxTime GetStart() const { return mStart; }
        FbxTime GetStop() const { return mStop; }
        FbxTime GetDuration() const { return FbxTime(mStop.GetMilliSeconds() - mStart.GetMilliSeconds()); }
    };
    FbxTimeSpan mLocalTimeSpan;
};

// ---- FbxDocumentInfo ----
class FbxDocumentInfo : public FbxObject
{
public:
    FbxProperty LastSavedUrl;
    FbxProperty Original_FileName;
};

// ---- FbxScene ----
class FbxScene : public FbxObject
{
public:
    static FbxScene* Create(FbxManager* pMgr, const char* name) { return nullptr; }
    FbxNode* GetRootNode() const { return nullptr; }
    FbxDocumentInfo* GetSceneInfo() const { return nullptr; }
    int GetAnimStackCount() const { return 0; }
    FbxAnimStack* GetAnimStack(int idx) const { return nullptr; }
    void SetCurrentAnimationStack(FbxAnimStack* pAnimStack) {}
    void FillAnimStackNameArray(FbxArray<FbxString*>& names) const {}
    FbxTakeInfo* GetTakeInfo(const char* name) const { return nullptr; }
    void Destroy() {}

    struct FbxGlobalSettings {
        FbxAxisSystem GetAxisSystem() const { return FbxAxisSystem(); }
        FbxTime::EMode GetTimeMode() const { return FbxTime::eFrames30; }
        FbxSystemUnit GetSystemUnit() const { return FbxSystemUnit(1.0); }
    };
    FbxGlobalSettings& GetGlobalSettings() { static FbxGlobalSettings s; return s; }
    const FbxGlobalSettings& GetGlobalSettings() const { static FbxGlobalSettings s; return s; }
};

// ---- FbxManager ----
class FbxManager
{
public:
    static FbxManager* Create() { return nullptr; }
    void Destroy() {}
    void SetIOSettings(FbxIOSettings* ios) {}
    FbxIOSettings* GetIOSettings() const { return nullptr; }
    static void GetFileFormatVersion(int& major, int& minor, int& rev) { major=2015; minor=1; rev=0; }
    const char* GetVersion(bool full=false) const { return "2015.1.0"; }
    void LoadPluginsDirectory(const char* path, const char* extensions=nullptr) {}
};

// ---- FbxImporter ----
class FbxImporter : public FbxIOBase
{
public:
    static FbxImporter* Create(FbxManager* pMgr, const char* name) { return nullptr; }
    bool Initialize(const char* filename, int format=-1, FbxIOSettings* ios=nullptr) { return false; }
    bool IsFBX() const { return false; }
    bool Import(FbxScene* pScene) { return false; }
    void ParseForGlobalSettings(bool val) {}
    void GetFileVersion(int& major, int& minor, int& rev) const {}
    FbxStatus& GetStatus() { static FbxStatus s; return s; }
    const char* GetLastErrorString() const { return ""; }
    int GetLastErrorID() const { return 0; }
    int GetAnimStackCount() const { return 0; }
    FbxTakeInfo* GetTakeInfo(int idx) const { return nullptr; }
    FbxString GetActiveAnimStackName() const { return FbxString(""); }
    void SetActiveAnimStackName(const char* name) {}
    // GetFrameRate fills the mode reference and returns true if successful
    bool GetFrameRate(FbxTime::EMode& mode) const { mode = FbxTime::eFrames30; return true; }
    void Destroy() {}
};

// ---- FbxGetApplicationDirectory ----
inline FbxString FbxGetApplicationDirectory() { return FbxString(""); }

// ---- FbxCast helper ----
// Single-arg: FbxCast<T>(FbxObject*)
template<class T>
inline T* FbxCast(FbxObject* obj) { return dynamic_cast<T*>(obj); }
// Two-arg version for compatibility
template<class T, class U>
inline T* FbxCast(U* obj) { return dynamic_cast<T*>(obj); }

// Static member definitions.
// __declspec(selectany) allows the definition to appear in multiple translation
// units (like an inline variable) without ODR violations on MSVC.
__declspec(selectany) const char* FbxSurfaceMaterial::sDiffuse = "DiffuseColor";
__declspec(selectany) const char* FbxSurfaceMaterial::sNormalMap = "NormalMap";

#pragma warning(pop)

#endif // _FBXSDK_H_
