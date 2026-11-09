// Gives the N-Gage runtime's process a Doug Lea-only thread heap, as on S60 3rd Edition.
#include <e32std.h>
#include <new>

extern "C" void __cxa_pure_virtual() { User::Invariant(); }

namespace {
// Cells allocated before the switch stay owned by the original heap.
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
        return (cell ? Owner(cell) : heap_)->ReAlloc(cell, size, mode);
    }
    TInt AllocLen(const TAny* cell) const override { return Owner(cell)->AllocLen(cell); }
    TInt Compress() override { return heap_->Compress() + original_->Compress(); }
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

// Symbian^3's hybrid heap serves large cells from fresh pages; games written for the
// S60 3rd Edition RHeap can depend on reused memory. A heap in a non-disconnected chunk
// is Doug Lea-only.
void Install() {
    RHeap& original = User::Heap();
    const TInt max_length = original.MaxLength();
    RChunk chunk;
    if (chunk.CreateLocal(0, max_length) != KErrNone) return;
    RHeap* heap = UserHeap::ChunkHeap(chunk, 0, 0x1000, max_length, 0, EFalse,
                                      UserHeap::EChunkHeapDuplicate);
    TAny* memory = heap ? heap->Alloc(sizeof(LegacyHeap)) : nullptr;
    if (!memory) {
        if (heap) heap->Close();
        chunk.Close();
        return;
    }
    User::SwitchAllocator(new (memory) LegacyHeap(&original, heap, chunk.Handle(), chunk.Base(),
                                                  chunk.MaxSize()));
}
}

extern "C" TInt NgageHeapEntry(TInt reason) {
    if (reason == 2) Install();
    return KErrNone;
}

extern "C" __attribute__((visibility("default"))) void CloseStdlib() {
    RLibrary library;
    if (library.Load(_L("estlib.dll")) != KErrNone) return;
    if (auto close = library.Lookup(1)) close();
    library.Close();
}
