#ifndef TILECACHE_H
#define TILECACHE_H
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <string>
#include <unordered_map>
#include <list>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <wininet.h>

struct TileKey{ int z=0,x=0,y=0; };
inline bool operator==(const TileKey&a,const TileKey&b){return a.z==b.z&&a.x==b.x&&a.y==b.y;}
struct TileKeyHash{
    size_t operator()(const TileKey& k) const noexcept {
		//these are large primes number multipliers to help spread out the hash values
		//this is useful for tile keys which often have similar x/y/z values
        return (size_t)k.z*73856093u ^ (size_t)k.x*19349663u ^ (size_t)k.y*83492791u;
    }
};

struct TileData { std::shared_ptr<const std::vector<BYTE>> png; std::list<TileKey>::iterator it; };
struct TileBitmap{ ID2D1Bitmap* bmp; std::list<TileKey>::iterator it; };

class TileCache {
public:
    TileCache();
    ~TileCache();
    std::wstring NormaliseEndpoint(std::wstring e);
    void Configure(const std::wstring& endpoint, const std::wstring& userAgent, size_t maxMemTiles,
                   int delayMs, int backoffStartMs, int backoffMaxMs);
    void SetFactories(IWICImagingFactory* wic, ID2D1HwndRenderTarget* rt);
    void SetNotify(HWND hwnd, UINT msg);
    void Start(int workers=1);
    void Stop();
    void Clear();
    void Enqueue(const TileKey& k);                       // normal priority
    void EnqueuePriority(const TileKey& k, int priority); // lower value = sooner
	void UpdateEndpoint(const std::wstring& endpoint); // allow changing endpoint on the fly
    int  Pending() const;
    bool TryGetBitmap(const TileKey& k, ID2D1Bitmap** out);
    std::wstring Attribution() const { return L"\u00A9 OpenStreetMap contributors"; }
private:
    std::wstring _endpoint, _ua;
    int _endpointVersion = 0; // Atomic-style versioning for cache consistency
    size_t _maxTiles=256;
    int _delay=150, _backoffStart=500, _backoffMax=4000;

    std::unordered_map<TileKey, TileData, TileKeyHash> _mem;
    std::list<TileKey> _memLRU;
    std::unordered_map<TileKey, TileBitmap, TileKeyHash> _bm; std::list<TileKey> _bmLRU; size_t _bmMax=512;
    std::vector<std::pair<int,TileKey>> _q; // (priority, key)

    mutable std::mutex _mx;
    std::condition_variable _cv;
    bool _quit=false;
    std::vector<std::thread> _ths;

    IWICImagingFactory* _wic=nullptr;
    ID2D1HwndRenderTarget* _rt=nullptr;
    HWND _notifyHwnd=nullptr; UINT _notifyMsg=0;

    HINTERNET _hInternet = NULL;

    bool Download(const std::wstring& url, std::vector<BYTE>& out);
    std::wstring MakeUrl(const TileKey& k) const;
    void Worker();
    void EvictIfNeeded();
};
#endif
