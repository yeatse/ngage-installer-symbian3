// Recognizes plaintext N-Gage game data so CAF's F32 agent can report a MIME
// type for it; protected DCF files and known formats still outrank these claims.
#include <apmrec.h>
#include <ecom/implementationproxy.h>

void operator delete(void* pointer) noexcept { User::Free(pointer); }
void operator delete(void* pointer, unsigned) noexcept { User::Free(pointer); }

namespace {

const TUid KRecognizerUid = {static_cast<TInt>(0xe0000966)};
_LIT(KExtension, ".ngdat");
_LIT(KPrivatePath, "\\private\\");
_LIT8(KDataType, "application/x-ngage-data");

class CNgageDataRecognizer : public CApaDataRecognizerType {
public:
    CNgageDataRecognizer() : CApaDataRecognizerType(KRecognizerUid, CApaDataRecognizerType::ENormal) {
        iCountDataTypes = 1;
    }

    TUint PreferredBufSize() override { return 0; }

    TDataType SupportedDataTypeL(TInt) const override { return TDataType(KDataType); }

private:
    void DoRecognizeL(const TDesC& aName, const TDesC8&) override {
        if (aName.Length() >= KExtension().Length()
                && aName.Right(KExtension().Length()).CompareF(KExtension) == 0) {
            iConfidence = EProbable;
            iDataType = TDataType(KDataType);
        } else if (aName.FindF(KPrivatePath) != KErrNotFound) {
            // Only the owning process can read a private file, so no other recognizer
            // can inspect it; game assets such as .dir and .dat archives land here.
            iConfidence = EPossible;
            iDataType = TDataType(KDataType);
        }
    }
};

CApaDataRecognizerType* CreateRecognizerL() { return new (ELeave) CNgageDataRecognizer; }

const TImplementationProxy KImplementations[] = {
    IMPLEMENTATION_PROXY_ENTRY(static_cast<TInt32>(0xe0000966), CreateRecognizerL)};

}  // namespace

__attribute__((visibility("default"))) const TImplementationProxy* ImplementationGroupProxy(TInt& aCount) {
    aCount = sizeof(KImplementations) / sizeof(KImplementations[0]);
    return KImplementations;
}
