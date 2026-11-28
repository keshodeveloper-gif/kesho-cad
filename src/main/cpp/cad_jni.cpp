#include <jni.h>
#include <android/log.h>
#include <cctype>
#include <exception>
#include <string>
#include <vector>

#include <Standard_Failure.hxx>
#include <Standard_Handle.hxx>
#include <Standard_Integer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <Quantity_Color.hxx>
#include <TDocStd_Document.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESControl_Controller.hxx>
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

struct Piece {
    std::string name;
    bool hasColor = false;
    float r = 0, g = 0, b = 0;
    std::vector<float> verts;
    std::vector<int> tris;
};

struct Ctx {
    Handle(XCAFDoc_ShapeTool) st;
    Handle(XCAFDoc_ColorTool) ct;
    double defl = 0.3;
    std::vector<Piece> out;
};

static std::string lowerStr(std::string s) {
    for (auto &c : s) c = (char) std::tolower((unsigned char) c);
    return s;
}

static bool endsWith(const std::string &s, const std::string &suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

static std::string labelName(const TDF_Label &lab) {
    Handle(TDataStd_Name) nm;
    if (lab.FindAttribute(TDataStd_Name::GetID(), nm)) {
        TCollection_AsciiString a(nm->Get(), '?');
        return std::string(a.ToCString());
    }
    return std::string();
}

static bool labelColor(const Handle(XCAFDoc_ColorTool) &ct, const TDF_Label &lab,
                       float &r, float &g, float &b) {
    Quantity_Color c;
    if (ct->GetColor(lab, XCAFDoc_ColorGen, c) ||
        ct->GetColor(lab, XCAFDoc_ColorSurf, c) ||
        ct->GetColor(lab, XCAFDoc_ColorCurv, c)) {
        Standard_Real R, G, B;
        c.Values(R, G, B, Quantity_TOC_sRGB);
        r = (float) R;
        g = (float) G;
        b = (float) B;
        return true;
    }
    return false;
}

/** يحوّل الـ triangulation لمثلثات بالمتر، Y-up (نفس منطق الكود القديم). */
static void extractMesh(const TopoDS_Shape &s, Piece &pc) {
    int base = 0;
    TopExp_Explorer ex(s, TopAbs_FACE);
    for (; ex.More(); ex.Next()) {
        TopoDS_Face face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;

        gp_Trsf trsf = loc.Transformation();
        int nn = tri->NbNodes();
        for (int i = 1; i <= nn; i++) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            pc.verts.push_back((float) (p.X() * 0.001));
            pc.verts.push_back((float) (p.Z() * 0.001));
            pc.verts.push_back((float) (-p.Y() * 0.001));
        }

        bool rev = face.Orientation() == TopAbs_REVERSED;
        for (int i = 1; i <= tri->NbTriangles(); i++) {
            int a, b, c;
            tri->Triangle(i).Get(a, b, c);
            if (rev) std::swap(b, c);
            pc.tris.push_back(base + a - 1);
            pc.tris.push_back(base + b - 1);
            pc.tris.push_back(base + c - 1);
        }
        base += nn;
    }
}

/**
 * يمشي على شجرة الملف:
 * - Reference: يدخل على الأصل ويضيف الـ location.
 * - Assembly: يمشي على أبنائه.
 * - Simple shape: ده جزء مستقل، بيتحول لقطعة.
 */
static void walk(Ctx &ctx, const TDF_Label &lab, const TopLoc_Location &loc,
                 const std::string &parentName, int depth) {
    if (depth > 40) return;

    std::string nm = labelName(lab);
    if (nm.empty()) nm = parentName;

    if (ctx.st->IsReference(lab)) {
        TDF_Label ref;
        ctx.st->GetReferredShape(lab, ref);
        walk(ctx, ref, loc * ctx.st->GetLocation(lab), nm, depth + 1);
        return;
    }

    if (ctx.st->IsAssembly(lab)) {
        TDF_LabelSequence comps;
        ctx.st->GetComponents(lab, comps);
        for (Standard_Integer i = 1; i <= comps.Length(); i++)
            walk(ctx, comps.Value(i), loc, nm, depth + 1);
        return;
    }

    if (!ctx.st->IsSimpleShape(lab)) return;

    TopoDS_Shape proto = ctx.st->GetShape(lab);
    if (proto.IsNull()) return;
    TopoDS_Shape placed = proto.Moved(loc);

    Piece pc;
    pc.name = nm;
    float r = 0, g = 0, b = 0;
    if (labelColor(ctx.ct, lab, r, g, b)) {
        pc.hasColor = true;
        pc.r = r;
        pc.g = g;
        pc.b = b;
    }

    BRepMesh_IncrementalMesh mesher(placed, ctx.defl, false, 0.5, true);
    mesher.Perform();
    extractMesh(placed, pc);

    if (!pc.tris.empty()) ctx.out.push_back(pc);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_kesho_blend_CadNative_ping(JNIEnv *env, jclass) {
    return env->NewStringUTF("native ok from CI");
}

/**
 * يرجّع Object[5]:
 *  [0] Object[] : كل عنصر float[] (رؤوس x,y,z بالمتر Y-up)
 *  [1] Object[] : كل عنصر int[] (مثلثات)
 *  [2] String[] : أسماء الأجزاء
 *  [3] float[]  : ألوان sRGB (r,g,b لكل جزء)، و-1 لو الجزء مفيهوش لون
 *  [4] String   : رسالة الخطأ (فاضية لو نجح)
 */
extern "C"
JNIEXPORT jobjectArray JNICALL
Java_com_kesho_blend_CadNative_importCadParts(JNIEnv *env, jclass, jstring jpath, jdouble deflectionMm) {
    const char *cp = env->GetStringUTFChars(jpath, nullptr);
    std::string path(cp);
    env->ReleaseStringUTFChars(jpath, cp);

    std::string err;
    Ctx ctx;
    ctx.defl = deflectionMm;

    try {
        std::string low = lowerStr(path);
        bool isStep = endsWith(low, ".stp") || endsWith(low, ".step");
        bool isIges = endsWith(low, ".igs") || endsWith(low, ".iges");

        Handle(TDocStd_Document) doc = new TDocStd_Document(TCollection_ExtendedString("BinXCAF"));

        if (isStep) {
            STEPCAFControl_Reader rd;
            rd.SetColorMode(Standard_True);
            rd.SetNameMode(Standard_True);
            rd.SetLayerMode(Standard_False);
            if (rd.ReadFile(path.c_str()) != IFSelect_RetDone) err = "مقدرتش أقرأ ملف STEP";
            else if (!rd.Transfer(doc)) err = "مقدرتش أحوّل ملف STEP";
        } else if (isIges) {
            IGESControl_Controller::Init();
            IGESCAFControl_Reader rd;
            rd.SetColorMode(Standard_True);
            rd.SetNameMode(Standard_True);
            rd.SetLayerMode(Standard_False);
            if (rd.ReadFile(path.c_str()) != IFSelect_RetDone) err = "مقدرتش أقرأ ملف IGES";
            else if (!rd.Transfer(doc)) err = "مقدرتش أحوّل ملف IGES";
        } else {
            err = "الصيغة مش مدعومة في المحرك الهندسي";
        }

        if (err.empty()) {
            ctx.st = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
            ctx.ct = XCAFDoc_DocumentTool::ColorTool(doc->Main());

            TDF_LabelSequence roots;
            ctx.st->GetFreeShapes(roots);
            for (Standard_Integer i = 1; i <= roots.Length(); i++)
                walk(ctx, roots.Value(i), TopLoc_Location(), std::string(), 0);

            if (ctx.out.empty()) err = "مفيش أجزاء مجسمة في الملف";
        }
    } catch (Standard_Failure &f) {
        err = std::string("خطأ OCCT: ") + f.GetMessageString();
    } catch (std::exception &e) {
        err = e.what();
    } catch (...) {
        err = "خطأ غير معروف أثناء القراءة";
    }

    const size_t n = ctx.out.size();
    jclass objCls = env->FindClass("java/lang/Object");
    jclass strCls = env->FindClass("java/lang/String");

    jobjectArray jverts = env->NewObjectArray((jsize) n, objCls, nullptr);
    jobjectArray jtris = env->NewObjectArray((jsize) n, objCls, nullptr);
    jobjectArray jnames = env->NewObjectArray((jsize) n, strCls, nullptr);
    std::vector<float> cols(n * 3, -1.0f);

    for (size_t i = 0; i < n; i++) {
        const Piece &pc = ctx.out[i];

        jfloatArray fv = env->NewFloatArray((jsize) pc.verts.size());
        env->SetFloatArrayRegion(fv, 0, (jsize) pc.verts.size(), pc.verts.data());
        env->SetObjectArrayElement(jverts, (jsize) i, fv);
        env->DeleteLocalRef(fv);

        jintArray iv = env->NewIntArray((jsize) pc.tris.size());
        env->SetIntArrayRegion(iv, 0, (jsize) pc.tris.size(), pc.tris.data());
        env->SetObjectArrayElement(jtris, (jsize) i, iv);
        env->DeleteLocalRef(iv);

        std::string nm = pc.name.empty() ? (std::string("جزء ") + std::to_string(i + 1)) : pc.name;
        jstring js = env->NewStringUTF(nm.c_str());
        env->SetObjectArrayElement(jnames, (jsize) i, js);
        env->DeleteLocalRef(js);

        if (pc.hasColor) {
            cols[i * 3] = pc.r;
            cols[i * 3 + 1] = pc.g;
            cols[i * 3 + 2] = pc.b;
        }
    }

    jfloatArray jcols = env->NewFloatArray((jsize) cols.size());
    env->SetFloatArrayRegion(jcols, 0, (jsize) cols.size(), cols.data());

    jobjectArray out = env->NewObjectArray(5, objCls, nullptr);
    env->SetObjectArrayElement(out, 0, jverts);
    env->SetObjectArrayElement(out, 1, jtris);
    env->SetObjectArrayElement(out, 2, jnames);
    env->SetObjectArrayElement(out, 3, jcols);
    env->SetObjectArrayElement(out, 4, env->NewStringUTF(err.c_str()));
    return out;
}
