#include <Windows.h>
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>

namespace
{
    // Converts handle value (64 or 32 bit) into 32 bit integer value.
    // NOTE: Highly implementation-dependent! Never use on other platforms.
    uint32_t ConvertHandle(HMIDIIN handle) {
        return reinterpret_cast<uint32_t>(handle);
    }

    // MIDI message structure.
    union Message
    {
        uint64_t uint64Value;

        struct
        {
            uint32_t source;
            uint8_t status;
            uint8_t data1;
            uint8_t data2;
        };

        Message(HMIDIIN aSource, uint8_t aStatus, uint8_t aData1, uint8_t aData2)
            :   source(ConvertHandle(aSource)), status(aStatus), data1(aData1), data2(aData2)
        {
        }
    };

    static_assert(sizeof(Message) == sizeof(uint64_t), "Wrong data size.");

    // MIDI device handle vector.
    std::vector<HMIDIIN> handles;

    // Incoming MIDI message queue.
    std::queue<Message> messageQueue;
    std::mutex messageQueueLock;

    // MIDI callback function.
    void CALLBACK MyMidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (wMsg == MIM_DATA) {
            // Retrieve the MIDI message data.
            uint8_t status = dwParam1 & 0xff;
            uint8_t data1 = (dwParam1 >> 8) & 0xff;
            uint8_t data2 = (dwParam1 >> 16) & 0xff;

            // Push to the message queue.
            messageQueueLock.lock();
            messageQueue.push(Message(hMidiIn, status, data1, data2));
            messageQueueLock.unlock();
        }
    }

    // Plug-in state update function.
    void ResetPluginIfRequired()
    {
        // Reset only if the number of devices was changed.
        int deviceCount = midiInGetNumDevs();
        if (deviceCount == handles.size()) return;

        // Close the all MIDI handles.
        for (auto& handle : handles) {
            midiInClose(handle);
        }
        handles.resize(0);

        // Clear the message queue.
        std::queue<Message> emptyQueue;
        std::swap(messageQueue, emptyQueue);

        // Enumerate the all MIDI devices.
        for (int i = 0; i < deviceCount; i++) {
            HMIDIIN handle;
            MMRESULT result = midiInOpen(&handle, i, reinterpret_cast<DWORD_PTR>(MyMidiInProc), NULL, CALLBACK_FUNCTION);
            assert(result == MMSYSERR_NOERROR);

            result = midiInStart(handle);
            assert(result == MMSYSERR_NOERROR);

            handles.push_back(handle);
        }
    }
}

// Exposed functions.

#define EXPORT_API __declspec(dllexport)

// Counts the number of endpoints.
extern "C" int EXPORT_API UnityMIDIReceiver_CountEndpoints()
{
    ResetPluginIfRequired();
    return static_cast<int>(handles.size());
}

// Get the unique ID of an endpoint.
extern "C" uint32_t EXPORT_API UnityMIDIReceiver_GetEndpointIDAtIndex(int index)
{
    if (index >= 0 && index < static_cast<int>(handles.size())) {
        return ConvertHandle(handles[index]);
    } else {
        return 0;
    }
}

// Get the name of an endpoint.
extern "C" const EXPORT_API char* UnityMIDIReceiver_GetEndpointName(uint32_t id)
{
    // Find the handle from the handle vector.
    auto it = std::find_if(handles.begin(), handles.end(), [&](HMIDIIN h){
        return ConvertHandle(h) == id;
    });
    if (it == handles.end()) return NULL;

    // Determine the device ID from the given endpoint ID.
    UINT deviceID;
    MMRESULT result = midiInGetID(*it, &deviceID);
    if (result != MMSYSERR_NOERROR) return NULL;

    // Retrieve the device caps.
    static MIDIINCAPS caps;
    result = midiInGetDevCaps(deviceID, &caps, sizeof(caps));
    if (result != MMSYSERR_NOERROR) return NULL;

    return caps.szPname;
}

// Retrieve and erase an MIDI message data from the message queue.
extern "C" uint64_t EXPORT_API UnityMIDIReceiver_DequeueIncomingData()
{
    ResetPluginIfRequired();

    if (messageQueue.empty()) return 0;

    messageQueueLock.lock();
    Message m = messageQueue.back();
    messageQueue.pop();
    messageQueueLock.unlock();

    return m.uint64Value;
}