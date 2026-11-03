#pragma once
#include <e32base.h>
#include <remconaddress.h>
#include <remconcoreapi.h>

namespace Ngage {
static_assert(KRemConCoreApiUid == 0x10205e60 && ERemConCoreApiVolumeUp == 0x41 && ERemConCoreApiVolumeDown == 0x42);
static_assert(ERemConCoreApiButtonPress == 0 && ERemConCoreApiButtonRelease == 1 && ERemConCoreApiButtonClick == 2);
// Wire layout follows RemCon RRemCon::Receive and CRemConInterfaceDetailsArray.
struct RemoteMessage { TUint uid, operation, subtype, address[12]; };
static_assert(sizeof(TRemConAddress) == 48 && sizeof(RemoteMessage) == 60);
class RemoteSession : public RSessionBase {
public:
    TInt Open() {
        TInt error = CreateSession(_L("!RemConSrv"), TVersion(2, 0, 2), 4);
        if (error == KErrNotFound || error == KErrServerTerminated) {
            RProcess server;
            error = server.Create(_L("Z:\\sys\\bin\\remconserver.exe"), KNullDesC);
            if (error != KErrNone) return error;
            TRequestStatus status; server.Rendezvous(status);
            if (status == KRequestPending) server.Resume(); else server.Kill(0);
            User::WaitForRequest(status);
            error = server.ExitType() == EExitPanic ? KErrServerTerminated : status.Int();
            server.Close();
            if (error != KErrNone && error != KErrAlreadyExists) return error;
            error = CreateSession(_L("!RemConSrv"), TVersion(2, 0, 2), 4);
        }
        return error;
    }
    TInt Call(TInt code, const TIpcArgs& args = TIpcArgs()) { return SendReceive(code, args); }
    void Receive(TDes8& package, TDes8& data, TRequestStatus& status) { SendReceive(13, TIpcArgs(&package, &data), status); }
};
class VolumeReceiver;
class VolumeRepeat final : public CActive {
    RTimer timer_;
    void (*change_)(TAny*, TInt);
    TAny* context_;
    TInt direction_ = 0;
    TInt refresh_budget_ = 0;
    void RunL() override {
        if (iStatus.Int() == KErrNone && direction_ && refresh_budget_-- > 0) {
            change_(context_, direction_); timer_.After(iStatus, 150000); SetActive();
        } else direction_ = 0;
    }
    void DoCancel() override { timer_.Cancel(); }
public:
    VolumeRepeat(void (*change)(TAny*, TInt), TAny* context)
        : CActive(EPriorityStandard), change_(change), context_(context) { CActiveScheduler::Add(this); }
    TInt Open() { return timer_.CreateLocal(); }
    void Start(TInt direction) {
        // Core API holds are refreshed at least once per second.
        refresh_budget_ = 8;
        if (direction_ == direction && IsActive()) return;
        Cancel(); direction_ = direction; change_(context_, direction_);
        timer_.After(iStatus, 500000); SetActive();
    }
    void Stop() { Cancel(); direction_ = 0; refresh_budget_ = 0; }
    ~VolumeRepeat() override { Cancel(); timer_.Close(); }
};
class VolumeReceiver final : public CActive {
    RemoteSession session_;
    TPckgBuf<RemoteMessage> message_;
    TBuf8<64> data_;
    VolumeRepeat repeat_;
    void (*change_)(TAny*, TInt);
    TAny* context_;
    void Receive() { data_.Zero(); session_.Receive(message_, data_, iStatus); SetActive(); }
    void RunL() override {
        const TInt error = iStatus.Int();
        if (error != KErrNone) { repeat_.Stop(); return; }
        TInt result = KErrNotSupported;
        const TUint operation = message_().operation;
        if (message_().uid == 0x10205e60 && (operation == 0x41 || operation == 0x42) && data_.Length() >= 1) {
            const TInt action = data_[0], direction = operation == 0x41 ? 1 : -1;
            result = KErrNone;
            if (action == 0) repeat_.Start(direction);
            else if (action == 1) repeat_.Stop();
            else if (action == 2) { repeat_.Stop(); change_(context_, direction); }
            else result = KErrArgument;
        }
        const TUint info[] = { operation, 0 };
        const TPtrC8 operation_info(reinterpret_cast<const TUint8*>(info), sizeof(info));
        const TPckgC<TInt> response(result);
        session_.Call(20, TIpcArgs(message_().uid, &operation_info, 0, &response));
        Receive();
    }
    void DoCancel() override { session_.Call(14); }
    VolumeReceiver(void (*change)(TAny*, TInt), TAny* context)
        : CActive(EPriorityStandard), repeat_(change, context), change_(change), context_(context) {
        CActiveScheduler::Add(this);
    }
    TInt Open() {
        TInt error = session_.Open();
        if (error != KErrNone) return error;
        TBuf8<25> registration;
        auto word = [&](TUint value) { registration.Append(TPtrC8(reinterpret_cast<const TUint8*>(&value), 4)); };
        word(1); word(0x10205e60); word(2); registration.Append(TChar(0)); word(8); word(0x41); word(0x42);
        error = session_.Call(19, TIpcArgs(&registration));
        if (error == KErrNone) error = repeat_.Open();
        if (error == KErrNone) Receive();
        return error;
    }
public:
    static VolumeReceiver* New(void (*change)(TAny*, TInt), TAny* context) {
        if (!CActiveScheduler::Current()) return nullptr;
        void* memory = User::AllocZ(sizeof(VolumeReceiver));
        if (!memory) return nullptr;
        auto* receiver = new(memory) VolumeReceiver(change, context);
        const TInt error = receiver->Open();
        if (error != KErrNone) { delete receiver; return nullptr; }
        return receiver;
    }
    ~VolumeReceiver() override { Cancel(); repeat_.Stop(); session_.Close(); }
};
}
