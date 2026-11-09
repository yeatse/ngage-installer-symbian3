// Gives the N-Gage runtime's process a Doug Lea-only thread heap, as on S60 3rd Edition.
#include <e32std.h>
#include <new>

extern "C" void __cxa_pure_virtual() { User::Invariant(); }

namespace {
struct HeapChunk : RHeap {
    static TInt Handle(const RHeap& heap) { return static_cast<const HeapChunk&>(heap).iChunkHandle; }
};

// Cells allocated before the switch stay owned by the original heap, which never grows again.
class LegacyHeap : public RHeap {
public:
    LegacyHeap(RHeap* original, RHeap* heap, TInt chunk, TUint8* base, TInt max_length)
        : original_(original), heap_(heap), chunk_(chunk) {
        iAccessCount = 1;
        // NGI pools read the chunk through iHandles; a zero count keeps Close from closing it.
        iHandleCount = 0;
        iHandles = &chunk_;
        iFlags = 0;
        iCellCount = 0;
        iTotalAllocSize = 0;
        iMaxLength = max_length;
        iBase = base;
        iTop = base + max_length;
        iAlign = ECellAlignment;
    }
    TAny* Alloc(TInt size) override { return heap_->Alloc(size); }
    void Free(TAny* cell) override { if (cell) Owner(cell)->Free(cell); }
    TAny* ReAlloc(TAny* cell, TInt size, TInt mode) override {
        if (!cell || Owner(cell) == heap_) return heap_->ReAlloc(cell, size, mode);
        const TInt length = original_->AllocLen(cell);
        if (size <= length) return original_->ReAlloc(cell, size, mode);
        if (mode & ENeverMove) return nullptr;
        TAny* moved = heap_->Alloc(size);
        if (moved) {
            Mem::Copy(moved, cell, length);
            original_->Free(cell);
        }
        return moved;
    }
    TInt AllocLen(const TAny* cell) const override { return Owner(cell)->AllocLen(cell); }
    TInt Compress() override { return heap_->Compress(); }
    void Reset() override { heap_->Reset(); }
    TInt AllocSize(TInt& total) const override {
        TInt own = 0, inherited = 0;
        const TInt count = heap_->AllocSize(own) + original_->AllocSize(inherited);
        total = own + inherited;
        return count;
    }
    TInt Available(TInt& biggest) const override { return heap_->Available(biggest); }
    TInt DebugFunction(TInt function, TAny* a1, TAny* a2) override {
        return heap_->DebugFunction(function, a1, a2);
    }
protected:
    TInt Extension_(TUint id, TAny*& a0, TAny* a1) override {
        return static_cast<MAllocator*>(heap_)->Extension_(id, a0, a1);
    }
private:
    RHeap* Owner(const TAny* cell) const {
        const TUint offset = static_cast<const TUint8*>(cell) - iBase;
        return offset < static_cast<TUint>(iMaxLength) ? heap_ : original_;
    }
    RHeap* original_;
    RHeap* heap_;
    TInt chunk_;
};

// Games known to depend on the S60 3rd Edition heap; others keep the hybrid heap.
const TUint32 KLegacyHeapGames[] = {
    0x2000AFE4,  // Brothers In Arms
};

bool NeedsLegacyHeap() {
    const TUint32 sid = RProcess().SecureId();
    for (const TUint32 game : KLegacyHeapGames) if (game == sid) return true;
    return false;
}

// Symbian^3's hybrid heap serves large cells from fresh pages; games written for the
// S60 3rd Edition RHeap can depend on reused memory. The hybrid heap keeps the lower half
// of its chunk for those pages, so a Doug Lea heap there keeps the game's low addresses.
// It is fully committed, like a fixed-size heap: games also read freed memory past its top.
void Install() {
    RHeap& original = User::Heap();
    RChunk chunk;
    chunk.SetHandle(HeapChunk::Handle(original));
    const TInt page = 0x1000;
    const TInt length = reinterpret_cast<TUint8*>(&original) - chunk.Base() - page;
    if (length < page) return;  // already a Doug Lea-only heap
    // A non-zero offset makes the heap Doug Lea-only.
    RHeap* heap = UserHeap::OffsetChunkHeap(chunk, length, page, page, length, 0, EFalse,
                                            UserHeap::EChunkHeapDuplicate);
    TAny* memory = heap ? heap->Alloc(sizeof(LegacyHeap)) : nullptr;
    if (!memory) {
        if (heap) heap->Close();
        return;
    }
    User::SwitchAllocator(new (memory) LegacyHeap(&original, heap, HeapChunk::Handle(*heap),
                                                  chunk.Base() + page, length));
}
}

extern "C" TInt NgageHeapEntry(TInt reason) {
    if (reason == 2 && NeedsLegacyHeap()) Install();
    return KErrNone;
}

extern "C" __attribute__((visibility("default"))) void CloseStdlib() {
    RLibrary library;
    if (library.Load(_L("estlib.dll")) != KErrNone) return;
    if (auto close = library.Lookup(1)) close();
    library.Close();
}
