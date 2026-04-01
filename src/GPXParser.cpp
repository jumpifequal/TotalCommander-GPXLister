#include "GPXParser.h"
#include <msxml6.h>
#include <atlbase.h>
#include <atlcomcli.h>
#include <cstdio>
#include <ctime>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

//the max amount of points for segment in a gpx track allowed. Is neede to avoid DoS and lock on drawing
#define GPX_MAX_POINTS_FOR_SEGMENT 500000

// Helper to parse doubles independent of system locale (fixes 0-speed bug on non-US locales). It's , vs . decimal separators 
static double ParseDoubleInvariant(const wchar_t* s) {
    if (!s) return 0.0;
    // Skip leading whitespace
    while (*s && (*s == L' ' || *s == L'\t')) s++;
    
    double sign = 1.0;
    if (*s == L'-') { sign = -1.0; s++; }
    else if (*s == L'+') { s++; }

    double val = 0.0;
    // Integer part
    while (*s >= L'0' && *s <= L'9') {
        val = val * 10.0 + (*s - L'0');
        s++;
    }
    // Fractional part
    if (*s == L'.') {
        s++;
        double frac = 0.1;
        while (*s >= L'0' && *s <= L'9') {
            val += (*s - L'0') * frac;
            frac *= 0.1;
            s++;
        }
    }
    return val * sign;
}

// Helper to parse integers and advance pointer (replaces lambda)
static int ParseIntInvariant(const wchar_t*& p) {
    // Skip non-digits
    while (*p && !(*p >= L'0' && *p <= L'9')) p++;
    
    int val = 0;
    while (*p >= L'0' && *p <= L'9') {
        int digit = (*p - L'0');
        if (val > (INT_MAX - digit) / 10) {
            // overflow. clamp and stop consuming digits to keep parser deterministic
            val = INT_MAX;
            while (*p >= L'0' && *p <= L'9') p++;
            return val;
        }
        val = val * 10 + digit;
        p++;
    }
    return val;
}

// Helper to manually parse ISO 8601 strings (Robust against format variations)
// Handles: "2024-10-25T12:00:00Z", "2024-10-25 12:00:00.123", etc.
// Fixed precision for speed profile calculation using mseconds too for the speed profile
static double ParseIso8601(const wchar_t* iso) {
    if (!iso) return 0.0;

    struct tm t = { 0 };
    int y = 0, M = 0, d = 0;
    double s = 0.0;

    const wchar_t* p = iso;
    y = ParseIntInvariant(p);
    M = ParseIntInvariant(p);
    d = ParseIntInvariant(p);
    t.tm_hour = ParseIntInvariant(p);
    t.tm_min = ParseIntInvariant(p);

    // 1. Estraiamo i secondi completi (es. 20.500)
    while (*p && !(*p >= L'0' && *p <= L'9')) p++;
    if (*p) s = ParseDoubleInvariant(p);

    t.tm_year = y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = d;
    t.tm_sec = 0; 
    t.tm_isdst = -1;

    time_t utc_time = _mkgmtime(&t);
    if (utc_time == -1) return 0.0;

    // 3. Sommiamo l'intero valore di 's' (secondi + decimali) alla base di minuti
    return (double)utc_time + s;
}

// Helper to extract string content from a child node via direct iteration.
// This is immune to XML namespace issues that often break XPath relative queries.
static bool GetChildString(IXMLDOMNode* parent, const wchar_t* localName, std::wstring& out) {
    if (!parent) return false;
    
    CComPtr<IXMLDOMNodeList> children;
    if (FAILED(parent->get_childNodes(&children)) || !children) return false;
    
    long length = 0;
    children->get_length(&length);
    
    for (long i = 0; i < length; ++i) {
        CComPtr<IXMLDOMNode> child;
        children->get_item(i, &child);
        
        CComBSTR bName;
        child->get_baseName(&bName); // baseName ignores namespace prefixes
        
        // If baseName is null (e.g. for text nodes), fallback to nodeName
        if (!bName) {
            child->get_nodeName(&bName);
        }
        if (bName && _wcsicmp(bName, localName) == 0) {
            CComBSTR text;
            if (SUCCEEDED(child->get_text(&text))) {
                out = (text.m_str ? text.m_str : L"");
                return true;
            }
        }
    }
    return false;
}

// Helper to extract text content from a child node
static bool GetChildDouble(IXMLDOMNode* parent, const wchar_t* localName, double& out) {
    std::wstring val;
    if (GetChildString(parent, localName, val) && !val.empty()) {
        out = ParseDoubleInvariant(val.c_str());
        return true;
    }
    return false;
}

static bool AttrDouble(IXMLDOMNode* n, const wchar_t* name, double& out){
    CComPtr<IXMLDOMNamedNodeMap> attrs;
    if(FAILED(n->get_attributes(&attrs)) || !attrs) return false;
    
    // Namespace-agnostic attribute retrieval
    long count = 0;
    attrs->get_length(&count);
    for (long i = 0; i < count; ++i) {
        CComPtr<IXMLDOMNode> attr;
        attrs->get_item(i, &attr);
        CComBSTR nodeName;
        attr->get_baseName(&nodeName); // baseName ignores prefix
        
        if (nodeName && _wcsicmp(nodeName, name) == 0) {
            CComVariant v;
            attr->get_nodeValue(&v);
            if (FAILED(v.ChangeType(VT_BSTR)) || v.bstrVal == nullptr) return false;
            out = ParseDoubleInvariant(v.bstrVal); // Use robust parser
            return true;
        }
    }
    return false;
}

// Purpose: Its sole job is to read the physical .gpx file (XML) and extract raw data (Latitude, Longitude, Elevation, Time) 
// into a simple intermediate structure called GpxTrack.
bool ParseGpxFile(const wchar_t* path, std::vector<GpxTrack>& out){ 
    CComPtr<IXMLDOMDocument2> doc;
    if(FAILED(CoCreateInstance(CLSID_DOMDocument60,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&doc)))) return false;
    
    // Explicitly set the selection language to XPath
    HRESULT hr = doc->setProperty(CComBSTR(L"SelectionLanguage"), CComVariant(L"XPath"));
    if (FAILED(hr)) return false;
    
    VARIANT_BOOL ok=VARIANT_FALSE;
    doc->put_async(VARIANT_FALSE);
    doc->put_resolveExternals(VARIANT_FALSE);
    doc->put_validateOnParse(VARIANT_FALSE);
    doc->setProperty(CComBSTR(L"ProhibitDTD"), CComVariant(VARIANT_TRUE));
    if(FAILED(doc->load(CComVariant(path), &ok)) || ok==VARIANT_FALSE) return false;

    // find trk/trkseg/trkpt (namespace-agnostic via local-name())
    CComPtr<IXMLDOMNodeList> trksegs;
    doc->selectNodes(CComBSTR(L"//*[local-name()='trkseg']"), &trksegs);
    if(!trksegs) return false;

    long nseg=0; trksegs->get_length(&nseg);
    for(long i=0;i<nseg;i++){
        CComPtr<IXMLDOMNode> seg; trksegs->get_item(i,&seg);
        if(!seg) continue;

        GpxTrack t;
        
        // Attempt to get track name from parent <trk> node
        CComPtr<IXMLDOMNode> parent;
        if (SUCCEEDED(seg->get_parentNode(&parent)) && parent) {
            GetChildString(parent, L"name", t.name);
        }

        CComPtr<IXMLDOMNodeList> pts;
        seg->selectNodes(CComBSTR(L"./*[local-name()='trkpt']"), &pts);
        if(!pts) continue;

        long np=0; pts->get_length(&np);
        const long kMaxPoints = GPX_MAX_POINTS_FOR_SEGMENT;
        //Avoid DoS for extremely long tracks (now is points for segment are hardwired to 500k) - Can be changed!
        if (np > kMaxPoints) np = kMaxPoints;
        t.pts.reserve(np);
        
        for(long j=0;j<np;j++){        
            CComPtr<IXMLDOMNode> p; pts->get_item(j, &p);
            double la = 0, lo = 0, el = 0;
            // Extract mandatory lat/lon attributes
            if (AttrDouble(p, L"lat", la) && AttrDouble(p, L"lon", lo)) {
                if (!(la >= -90.0 && la <= 90.0 && lo >= -180.0 && lo <= 180.0)) {
                    continue; // invalid coordinates
                }
                // Extract elevation if present using the robust local-name helper
                GetChildDouble(p, L"ele", el); 
                
                // Extract time
                double tm = 0.0;
                std::wstring timeStr;
                if (GetChildString(p, L"time", timeStr)) {
                    tm = ParseIso8601(timeStr.c_str());
                }

                t.pts.push_back({ la, lo, el, tm });
            }            
        }
        if(!t.pts.empty()) out.push_back(std::move(t));
    }
    return !out.empty();
}

// Parse waypoints from a GPX file
bool ParseGpxWaypoints(const wchar_t* path, std::vector<GpxWaypoint>& out) {
    CComPtr<IXMLDOMDocument2> doc;
    if (FAILED(CoCreateInstance(CLSID_DOMDocument60, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&doc)))) return false;

    // Explicitly set the selection language to XPath
    HRESULT hr = doc->setProperty(CComBSTR(L"SelectionLanguage"), CComVariant(L"XPath"));
    if (FAILED(hr)) return false;

    VARIANT_BOOL ok = VARIANT_FALSE;
    doc->put_async(VARIANT_FALSE);
    // Security hardening: block DTD/externals to prevent XXE and entity expansion DoS
    doc->put_resolveExternals(VARIANT_FALSE);
    doc->put_validateOnParse(VARIANT_FALSE);
    doc->setProperty(CComBSTR(L"ProhibitDTD"), CComVariant(VARIANT_TRUE));
    if (FAILED(doc->load(CComVariant(path), &ok)) || ok == VARIANT_FALSE) return false;

    // Find all waypoint nodes
    CComPtr<IXMLDOMNodeList> wpts;
    doc->selectNodes(CComBSTR(L"//*[local-name()='wpt']"), &wpts);
    if (!wpts) return false;

    long nWpt = 0; wpts->get_length(&nWpt);
    out.reserve(nWpt);

    for (long i = 0; i < nWpt; i++) {
        CComPtr<IXMLDOMNode> node; wpts->get_item(i, &node);
        if (!node) continue;

        GpxWaypoint w;
        if (AttrDouble(node, L"lat", w.lat) && AttrDouble(node, L"lon", w.lon)) {
            GetChildDouble(node, L"ele", w.ele);
            GetChildString(node, L"name", w.name);
            GetChildString(node, L"sym", w.sym);

			// Extract time also for waypoints if present
            std::wstring timeStr;
            if (GetChildString(node, L"time", timeStr)) {
                w.time = ParseIso8601(timeStr.c_str());
            }
            
            out.push_back(w);
        }
    }
    return !out.empty();
}