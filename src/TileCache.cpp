#include "TileCache.h"
#include <wininet.h>
#include <atlbase.h>
#include <atlcomcli.h>
#include <algorithm>

#pragma comment(lib, "wininet.lib")

TileCache::TileCache(){}
TileCache::~TileCache(){ Stop(); Clear(); }

std::wstring TileCache::NormaliseEndpoint(std::wstring e) {
    // Safer default: require TLS. If you must support http for LAN, make it an explicit opt-in.
    if (e.rfind(L"https://", 0) == 0) return e;
    if (e.rfind(L"//", 0) == 0) return L"https:" + e;
    // very simple heuristic: treat as host/path
    if (e.find(L".") != std::wstring::npos) return L"https://" + e;
    return L""; // reject
}

void TileCache::Configure(const std::wstring& endpoint, const std::wstring& userAgent, size_t maxMemTiles,
                          int delayMs, int backoffStartMs, int backoffMaxMs){
    _endpoint = NormaliseEndpoint(endpoint);
    _ua = userAgent; _maxTiles = maxMemTiles;
    _delay = delayMs; _backoffStart = backoffStartMs; _backoffMax = backoffMaxMs; _bmMax = (size_t)std::max((int)_bmMax, (int)maxMemTiles);
}
void TileCache::SetFactories(IWICImagingFactory* wic, ID2D1HwndRenderTarget* rt){ _wic=wic; _rt=rt; }
void TileCache::SetNotify(HWND hwnd, UINT msg) {
    std::lock_guard<std::mutex> lk(_mx); _notifyHwnd = hwnd; _notifyMsg = msg;
}

void TileCache::Start(int workers){
    Stop(); _quit=false;
    if (!_hInternet) 
        _hInternet = InternetOpenW(_ua.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if(workers<1) workers=1; 
    if(workers>8) workers=8;
    _ths.clear(); _ths.reserve(workers);
    if (_hInternet) { 
        InternetCloseHandle(_hInternet); 
        _hInternet = NULL; 
    }
    for(int i=0;i<workers;i++) _ths.emplace_back([this]{ Worker(); });
}
void TileCache::Stop(){
    {
        std::lock_guard<std::mutex> lk(_mx);
        _quit=true;
    }
    _cv.notify_all();
    for(auto& t:_ths){ if(t.joinable()) t.join(); }
    _ths.clear();
}
void TileCache::Clear(){
    std::lock_guard<std::mutex> lk(_mx);
    _mem.clear();
    _memLRU.clear();
    for(auto& kv:_bm){ if(kv.second.bmp) kv.second.bmp->Release(); }
    _bm.clear(); _bmLRU.clear();
    _q.clear();
}

void TileCache::Enqueue(const TileKey& k){ EnqueuePriority(k, 1000000); }

void TileCache::EnqueuePriority(const TileKey& k, int priority){
    std::lock_guard<std::mutex> lk(_mx);
    if (_quit) return;
    if(_mem.find(k)!=_mem.end()) return;
    // dedup in queue
    for(const auto& it:_q){ if(it.second==k) return; }
    _q.emplace_back(priority, k);
    if(_q.size()>2048){
        auto farIt = std::max_element(_q.begin(), _q.end(), [](const auto& a, const auto& b){ return a.first < b.first; });
        if(farIt!=_q.end()) _q.erase(farIt);
    }
    _cv.notify_one();
}
int TileCache::Pending() const{
    std::lock_guard<std::mutex> lk(_mx);
    return (int)_q.size();
}

std::wstring TileCache::MakeUrl(const TileKey& k) const{
    
    // Snapshot endpoint once for consistency
    std::wstring url;
    {
        std::lock_guard<std::mutex> lk(_mx);
        url = _endpoint;
    }
    auto rep=[&](const wchar_t* tag, int v){
        size_t p=0; std::wstring t=tag;
        while((p=url.find(t,p))!=std::wstring::npos){ url.replace(p,t.size(), std::to_wstring(v)); p+=1; }
    };
    rep(L"{z}",k.z); rep(L"{x}",k.x); rep(L"{y}",k.y);
    return url;
}

bool TileCache::Download(const std::wstring& url, std::vector<BYTE>& out){
    
    if (url.empty()) { OutputDebugStringW(L"[TileCache] empty url\n"); return false; }

    HINTERNET hi = InternetOpenW(_ua.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hi) { OutputDebugStringW(L"[TileCache] InternetOpenW failed\n"); return false; }
    _hInternet = hi;
    if (!hi) return false;

    // Timeouts: keep the UI responsive even if the endpoint hangs.
    DWORD ms = 8000;
    (void)InternetSetOptionW(hi, INTERNET_OPTION_CONNECT_TIMEOUT, &ms, sizeof(ms));
    (void)InternetSetOptionW(hi, INTERNET_OPTION_SEND_TIMEOUT, &ms, sizeof(ms));
    (void)InternetSetOptionW(hi, INTERNET_OPTION_RECEIVE_TIMEOUT, &ms, sizeof(ms));

    HINTERNET hc = InternetOpenUrlW(hi, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if(!hc){ InternetCloseHandle(hi); return false; }
    
    static const size_t kMaxTileBytes = 4u * 1024u * 1024u; // hard cap to avoid OOM on bad endpoints

    // Use vector to avoid stack overflow warning (32KB stack usage)
    std::vector<BYTE> buf(32 * 1024); DWORD rd = 0;
    out.clear();

    out.reserve(256 * 1024);

    while (InternetReadFile(hc, buf.data(), (DWORD)buf.size(), &rd) && rd) {
        if (out.size() + (size_t)rd > kMaxTileBytes) {
            out.clear(); 
            InternetCloseHandle(hc);
            InternetCloseHandle(hi);
            return false;
        }
        out.insert(out.end(), buf.data(), buf.data() + rd);
    }
    InternetCloseHandle(hc); InternetCloseHandle(hi);
    
    return !out.empty();
}

void TileCache::EvictIfNeeded(){
    // _mem is PNG-bytes cache. Keep it deterministic (LRU), not unordered_map::begin().
    while(_mem.size() > _maxTiles){
        if (_memLRU.empty()) break;
        TileKey victim = _memLRU.back();
        _memLRU.pop_back();
        auto it = _mem.find(victim);
        if (it != _mem.end()) {
            _mem.erase(it);
        }
    }
    while(_bm.size() > _bmMax){
        if(_bmLRU.empty()) break;
        TileKey old = _bmLRU.back(); _bmLRU.pop_back();
        auto it=_bm.find(old); 
        if(it!=_bm.end()){ 
            if(it->second.bmp) 
                it->second.bmp->Release(); 
            _bm.erase(it);
        } 
     }

}

void TileCache::Worker() {
    int backoff = _backoffStart;
    
    // Standard PNG signature: 89 50 4E 47 0D 0A 1A 0A
    const BYTE pngSig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    // Standard JPEG signature prefix: FF D8 FF
    const BYTE jpgSig[] = { 0xFF, 0xD8, 0xFF };

    for (;;) {
        TileKey k{};
        bool has = false;
        int versionAtStart = 0;
        {
            std::unique_lock<std::mutex> lk(_mx);
            _cv.wait(lk, [&] { return _quit || !_q.empty(); });
            if (_quit) return;
            auto it = std::min_element(_q.begin(), _q.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            if (it != _q.end()) {
                k = it->second;
                versionAtStart = _endpointVersion;
                _q.erase(it);
                has = true;
            }
        }
        if (!has) continue;

        std::wstring url = MakeUrl(k);
        std::vector<BYTE> png;

        // 1. Download
        bool ok = Download(url, png);

        // 2. CRITICAL FIX: Validate Header BEFORE storing
        // If it's an HTML 404 page, this check fails.
        bool looksLikePng = ok && png.size() >= 8 && memcmp(png.data(), pngSig, 8) == 0;
        bool looksLikeJpg = ok && png.size() >= 3 && memcmp(png.data(), jpgSig, 3) == 0;
        if (looksLikePng || looksLikeJpg) {
            backoff = _backoffStart;
            {
                std::lock_guard<std::mutex> lk(_mx);
                if (versionAtStart == _endpointVersion) {
                    // Maintain LRU for PNG byte-cache
                    auto it = _mem.find(k);
                    if (it != _mem.end()) {
                        _memLRU.erase(it->second.it);
                        _mem.erase(it);
                    }
                    _memLRU.push_front(k);
                    _mem[k] = TileData{ std::make_shared<std::vector<BYTE>>(std::move(png)), _memLRU.begin() };
                    EvictIfNeeded();
                }
            }
            HWND nh = NULL;
            UINT nm = 0;
            {
                std::lock_guard<std::mutex> lk(_mx);
                nh = _notifyHwnd;
                nm = _notifyMsg;
            }
            if (nh && nm) PostMessageW(nh, nm, 0, MAKELPARAM(k.x, k.y));
        }
        else {
            // It was a download error OR garbage data (HTML/Text).
            // Do NOT store in _mem. Increase backoff to prevent network spam.
            backoff = std::min(_backoffMax, backoff * 2);
            OutputDebugStringW(L"[TileCache] Tile download invalid content. Dropped.\n");
        }
        Sleep(_delay + backoff / 8);
    }
}

bool TileCache::TryGetBitmap(const TileKey& k, ID2D1Bitmap** out) {
    if (!out) return false;

    // 1. Check Bitmap Cache
    {
        std::lock_guard<std::mutex> lk(_mx);
        auto it = _bm.find(k);
        if (it != _bm.end()) {
            _bmLRU.erase(it->second.it);
            _bmLRU.push_front(k);
            it->second.it = _bmLRU.begin();
            *out = it->second.bmp; if (*out) (*out)->AddRef(); return true;
        }
    }

    // 2. Check Memory Cache
    std::vector<BYTE> pngData;
    {
        std::lock_guard<std::mutex> lk(_mx);
        auto it2 = _mem.find(k);
        if (it2 == _mem.end()) return false; // Not downloaded yet
        pngData = *it2->second.png; // Copy is now avoidable. Better: decode from shared_ptr directly.
    }

    if (!_wic || !_rt) return false;

    // 3. Attempt Decode
    ID2D1Bitmap* bmp = nullptr;
    CComPtr<IWICStream> stream;
    CComPtr<IWICBitmapDecoder> dec;
    CComPtr<IWICBitmapFrameDecode> frame;
    CComPtr<IWICFormatConverter> conv;

    HRESULT hr = _wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory((BYTE*)pngData.data(), (DWORD)pngData.size());
    if (SUCCEEDED(hr)) hr = _wic->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnDemand, &dec);
    if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = _wic->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = _rt->CreateBitmapFromWicBitmap(conv, NULL, &bmp);

    bool isValid = SUCCEEDED(hr) && bmp;

    // 4. CRITICAL FIX: If decode failed, create a DUMMY transparent bitmap.
    // This stops the UI from infinitely requesting the bad tile.
    if (!isValid) {
        D2D1_SIZE_U sz = D2D1::SizeU(256, 256);
        D2D1_BITMAP_PROPERTIES props;
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = 96.0f; props.dpiY = 96.0f;
        _rt->CreateBitmap(sz, NULL, 0, props, &bmp);
        // Do not cache dummy. Return it once to avoid hard loops, but keep retry possible later.
    }

    // 5. Insert into Cache (Real only)
    if (bmp && isValid) {
        std::lock_guard<std::mutex> lk(_mx);
        // Double check if inserted by other thread
        auto it = _bm.find(k);
        if (it != _bm.end()) {
            bmp->Release(); // Use the one in cache
            *out = it->second.bmp;
            (*out)->AddRef();
        }
        else {
            _bmLRU.push_front(k);
            _bm[k] = TileBitmap{ bmp, _bmLRU.begin() }; // Takes ownership
            EvictIfNeeded();
            *out = bmp;
            (*out)->AddRef(); // AddRef for output
        }
        return true;
    }

    return false;
}

//implementation of UpdateEndpoint
void TileCache::UpdateEndpoint(const std::wstring& endpoint) {
    std::lock_guard<std::mutex> lk(_mx);
    _endpoint = NormaliseEndpoint(endpoint);
    if (_endpoint.empty()) OutputDebugStringW(L"[TileCache] endpoint rejected. tiles disabled.\n");
    _endpointVersion++; // Increment version to invalidate in-flight downloads
    _q.clear(); // Drop queued work for old endpoint
}
