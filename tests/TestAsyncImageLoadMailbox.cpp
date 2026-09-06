#include "Core/FAsyncImageLoader.h"

#include <cstdio>
#include <string>

namespace
{
    int GFailures = 0;
    constexpr int32_t kRapidRequestCount = 8;

    void Check(bool bCondition, const char* Message)
    {
        std::printf("%-64s %s\n", Message, bCondition ? "OK" : "**FAIL**");

        if (!bCondition)
        {
            ++GFailures;
        }
    }

    FImageLoadRequest MakeRequest(const char* Path)
    {
        FImageLoadRequest request;
        request.FilePath = Path ? Path : "";
        request.Attempts.push_back(FImageLoadAttempt{});
        return request;
    }
}

int main()
{
    FImageLoadRequestMailbox mailbox;
    Check(!mailbox.HasPending(), "mailbox starts empty");
    Check(mailbox.GetLatestRequestId() == 0, "zero is reserved before first submission");

    const uint64_t firstId = mailbox.Submit(MakeRequest("first"));
    Check(firstId != 0, "first submission receives non-zero generation");
    Check(mailbox.HasPending(), "submission creates pending work");
    Check(mailbox.IsLatest(firstId), "first generation is current");

    const uint64_t secondId = mailbox.Submit(MakeRequest("second"));
    Check(secondId > firstId, "generations increase monotonically");
    Check(!mailbox.IsLatest(firstId), "new submission invalidates prior generation");

    FImageLoadRequest taken;
    Check(mailbox.TakePending(taken), "latest pending request can be taken");
    Check(taken.RequestId == secondId, "take returns newest generation only");
    Check(taken.FilePath == "second", "take returns newest payload only");
    Check(taken.EnqueuedAt.time_since_epoch().count() != 0, "submit records queue timestamp");
    Check(!mailbox.HasPending(), "take empties pending slot");
    Check(!mailbox.TakePending(taken), "empty mailbox cannot be taken twice");

    const uint64_t activeId = mailbox.Submit(MakeRequest("active"));
    Check(mailbox.TakePending(taken), "request can transition to active state");
    mailbox.Cancel();
    Check(!mailbox.IsLatest(activeId), "cancel invalidates active generation");
    Check(!mailbox.HasPending(), "cancel clears pending request");

    uint64_t newestId = 0;

    for (int32_t index = 0; index < kRapidRequestCount; ++index)
    {
        newestId = mailbox.Submit(MakeRequest(std::to_string(index).c_str()));
    }

    Check(mailbox.TakePending(taken), "rapid submissions retain one pending request");
    Check(taken.RequestId == newestId, "rapid submissions retain latest generation");
    Check(
        taken.FilePath == std::to_string(kRapidRequestCount - 1),
        "rapid submissions retain latest payload");
    Check(!mailbox.HasPending(), "mailbox remains memory-bounded after rapid input");

    return GFailures == 0 ? 0 : 1;
}
