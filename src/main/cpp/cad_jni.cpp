#include <jni.h>
#include <android/log.h>
#include <cctype>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <Standard_Failure.hxx>
#include <STEPControl_Reader.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopAbs.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#define TAG "KeshoCad"

static jobjectArray makeResult(JNIEnv *env, jfloatArray jv, jintArray ji, const std::string &err) {
    jclass objCls = env->FindClass("java/lang/Object");
    jobjectArray out = env->NewObjectArray(3, objCls, nullptr);
    env->SetObjectArrayElement(out, 0, jv);
    env->SetObjectArrayElement(out, 1, ji);
    env->SetObjectArrayElement(out, 2, env->NewStringUTF(err.c_str()));
    return out;
}

static std::string lowerStr(std::string s) {
    for (auto &c : s) c = (char) std::tolower((unsigned char) c);
    return s;
}

static bool endsWith(const std::string &s, const std::string &suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

/** يقرأ STEP أو IGES ويرجّع الـ shape. err بيتملى لو فيه مشكلة. */
static TopoDS_Shape readShape(const std::string &path, std::string &err) {
    TopoDS_Shape shape;
    std::string p = lowerStr(path);

    if (endsWith(p, ".stp") || endsWith(p, ".step")) {
        STEPControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
            err = "مقدرتش أقرأ ملف STEP";
            return shape;
        }
        reader.TransferRoots();
        shape = reader.OneShape();
    } else if (endsWith(p, ".igs") || endsWith(p, ".iges")) {
        IGESControl_Controller::Init();
        IGESControl_Reader reader;
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
            err = "مقدرتش أقرأ ملف IGES";
            return shape;
        }
        reader.TransferRoots();
        shape = reader.OneShape();
    } else {
        err = "الصيغة مش مدعومة";
        return shape;
    }

    if (shape.IsNull()) err = "الملف فاضي أو مفيهوش أسطح";
    return shape;
}

/**
 * يحوّل STEP/IGES لشبكة مثلثات.
 * deflectionMm: أقصى انحراف بالمليمتر (أصغر = تفاصيل أكتر = مثلثات أكتر).
 * بيرجّع Object[3]: float[] للرؤوس (x,y,z بالمتر، Y-up)، int[] للمثلثات، String للخطأ.
 */
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_com_kesho_blend_CadNative_importCad(JNIEnv *env, jclass, jstring jpath, jdouble deflectionMm) {
    const char *cp = env->GetStringUTFChars(jpath, nullptr);
    std::string path(cp);
    env->ReleaseStringUTFChars(jpath, cp);

    std::string err;
    std::vector<float> verts;
    std::vector<int> tris;

    try {
        TopoDS_Shape shape = readShape(path, err);
        if (err.empty()) {
            BRepMesh_IncrementalMesh mesher(shape, deflectionMm, false, 0.5, true);
            mesher.Perform();

            int base = 0;
            TopExp_Explorer ex(shape, TopAbs_FACE);
            for (; ex.More(); ex.Next()) {
                TopoDS_Face face = TopoDS::Face(ex.Current());
                TopLoc_Location loc;
                Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
                if (tri.IsNull()) continue;

                gp_Trsf trsf = loc.Transformation();
                int nn = tri->NbNodes();
                for (int i = 1; i <= nn; i++) {
                    gp_Pnt p = tri->Node(i).Transformed(trsf);
                    // OCCT: Z-up بالمليمتر -> محرر التطبيق: Y-up بالمتر
                    verts.push_back((float) (p.X() * 0.001));
                    verts.push_back((float) (p.Z() * 0.001));
                    verts.push_back((float) (-p.Y() * 0.001));
                }

                bool rev = face.Orientation() == TopAbs_REVERSED;
                for (int i = 1; i <= tri->NbTriangles(); i++) {
                    int a, b, c;
                    tri->Triangle(i).Get(a, b, c);
                    if (rev) std::swap(b, c);
                    tris.push_back(base + a - 1);
                    tris.push_back(base + b - 1);
                    tris.push_back(base + c - 1);
                }
                base += nn;
            }
            if (tris.empty()) err = "مفيش أسطح اتحولت لمثلثات";
        }
    } catch (Standard_Failure &f) {
        err = std::string("خطأ OCCT: ") + f.GetMessageString();
    } catch (std::exception &e) {
        err = e.what();
    } catch (...) {
        err = "خطأ غير معروف أثناء الاستيراد";
    }

    if (!err.empty()) return makeResult(env, nullptr, nullptr, err);

    jfloatArray jv = env->NewFloatArray((jsize) verts.size());
    env->SetFloatArrayRegion(jv, 0, (jsize) verts.size(), verts.data());
    jintArray ji = env->NewIntArray((jsize) tris.size());
    env->SetIntArrayRegion(ji, 0, (jsize) tris.size(), tris.data());
    return makeResult(env, jv, ji, "");
}
