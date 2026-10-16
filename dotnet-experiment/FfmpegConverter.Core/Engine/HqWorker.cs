using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using FfmpegConverter.Core.Presets;

namespace FfmpegConverter.Core.Engine;

public static class HqWorker
{
    private const int MaxStatusLength = 32 * 1024 * 1024;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void StatusCallback(IntPtr jsonUtf8, long jsonLength, IntPtr userContext);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate IntPtr CreateHandle();

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void DestroyHandle(IntPtr handle);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int RunJob(
        IntPtr handle, IntPtr input, IntPtr workDir, IntPtr codec, IntPtr preset,
        StatusCallback callback, IntPtr userContext);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int RequestStop(IntPtr handle);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int RequestKill(IntPtr handle);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate int GetLastError(IntPtr handle, out IntPtr messageUtf8, out long messageLength);

    public static async Task<int> RunAsync(string[] args)
    {
        if (args.Length != 7)
            return 2;

        string runtimeRoot = args[1];
        string pipeName = args[2];
        string inputPath = args[3];
        string workDir = args[4];
        string codec = args[5];
        string preset = args[6];
        HqRuntimeLayout layout;
        try
        {
            layout = new HqRuntimeLayout(runtimeRoot);
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or NotSupportedException)
        {
            return 2;
        }

        Environment.SetEnvironmentVariable("HQ_PROJECT_ROOT", layout.Root);
        Environment.SetEnvironmentVariable("HQ_ENCODER_V2_TOOLCHAIN_INI", layout.ConfigurationPath);
        Environment.SetEnvironmentVariable("HQ_ENCODER_TOOLCHAIN_INI", layout.ConfigurationPath);
        Environment.SetEnvironmentVariable("HQ_SEGMENTATOR_TOOLCHAIN_INI", layout.SegmentatorConfigurationPath);

        await using var pipe = new System.IO.Pipes.NamedPipeClientStream(
            ".", pipeName, System.IO.Pipes.PipeDirection.Out,
            System.IO.Pipes.PipeOptions.Asynchronous);
        try
        {
            await pipe.ConnectAsync(20_000);
        }
        catch
        {
            return 3;
        }

        using var writer = new StreamWriter(pipe) { AutoFlush = true };
        object writeLock = new();
        void Send(object message)
        {
            lock (writeLock)
                writer.WriteLine(JsonSerializer.Serialize(message));
        }

        IntPtr library = IntPtr.Zero;
        IntPtr handle = IntPtr.Zero;
        RequestStop? requestStop = null;
        RequestKill? requestKill = null;
        object handleLock = new();
        CancellationTokenSource stopReaderCancellation = new();

        try
        {
            string? layoutError = layout.GetValidationError();
            if (layoutError != null)
            {
                Send(new { type = "result", result = 2, error = layoutError });
                return 0;
            }

            library = NativeLibrary.Load(layout.LibraryPath);
            var createHandle = Load<CreateHandle>(library, "hqclib_create_handle");
            var destroyHandle = Load<DestroyHandle>(library, "hqclib_destroy_handle");
            var runJob = Load<RunJob>(library, "hqclib_run_job");
            requestStop = Load<RequestStop>(library, "hqclib_request_stop");
            requestKill = Load<RequestKill>(library, "hqclib_request_kill");
            var getLastError = Load<GetLastError>(library, "hqclib_get_last_error");
            handle = createHandle();
            if (handle == IntPtr.Zero)
                throw new InvalidOperationException("HQ library failed to create a job handle.");

            IntPtr jobHandle = handle;
            RequestStop stopFunction = requestStop;
            RequestKill killFunction = requestKill;
            _ = Task.Run(async () =>
            {
                try
                {
                    while (!stopReaderCancellation.IsCancellationRequested &&
                           await Console.In.ReadLineAsync(stopReaderCancellation.Token) is { } command)
                    {
                        if (command is not ("stop" or "kill"))
                            continue;
                        lock (handleLock)
                        {
                            if (handle != IntPtr.Zero)
                            {
                                if (command == "kill")
                                    killFunction(handle);
                                else
                                    stopFunction(handle);
                            }
                        }
                    }
                }
                catch (OperationCanceledException) { }
                catch (IOException) { }
                catch (InvalidOperationException) { }
            });

            var callback = new StatusCallback((payload, length, _) =>
            {
                if (payload == IntPtr.Zero || length < 0 || length > MaxStatusLength)
                    return;
                string json = Marshal.PtrToStringUTF8(payload, checked((int)length)) ?? "";
                Send(new { type = "status", payload = json });
            });

            using var input = new Utf8NativeString(inputPath);
            using var work = new Utf8NativeString(workDir);
            using var codecUtf8 = new Utf8NativeString(codec);
            using var presetUtf8 = new Utf8NativeString(preset);
            int result = runJob(jobHandle, input.Pointer, work.Pointer, codecUtf8.Pointer,
                presetUtf8.Pointer, callback, IntPtr.Zero);

            string error = "";
            if (getLastError(jobHandle, out IntPtr errorPointer, out long errorLength) == 0 &&
                errorPointer != IntPtr.Zero && errorLength > 0)
                error = Marshal.PtrToStringUTF8(errorPointer, checked((int)Math.Min(errorLength, 4096))) ?? "";

            lock (handleLock)
                handle = IntPtr.Zero;
            destroyHandle(jobHandle);
            stopReaderCancellation.Cancel();
            Send(new { type = "result", result, error });
            return 0;
        }
        catch (Exception ex)
        {
            Send(new { type = "result", result = 2, error = ex.Message });
            return 0;
        }
        finally
        {
            stopReaderCancellation.Cancel();
            if (handle != IntPtr.Zero && library != IntPtr.Zero)
            {
                try
                {
                    var destroyHandle = Load<DestroyHandle>(library, "hqclib_destroy_handle");
                    destroyHandle(handle);
                }
                catch { }
            }
            if (library != IntPtr.Zero)
                NativeLibrary.Free(library);
        }
    }

    private static T Load<T>(IntPtr library, string symbol) where T : Delegate
    {
        if (!NativeLibrary.TryGetExport(library, symbol, out IntPtr address))
            throw new EntryPointNotFoundException($"HQ library is missing required symbol '{symbol}'.");
        return Marshal.GetDelegateForFunctionPointer<T>(address);
    }

    private sealed class Utf8NativeString : IDisposable
    {
        public Utf8NativeString(string value) => Pointer = Marshal.StringToCoTaskMemUTF8(value);
        public IntPtr Pointer { get; private set; }
        public void Dispose()
        {
            if (Pointer != IntPtr.Zero)
            {
                Marshal.FreeCoTaskMem(Pointer);
                Pointer = IntPtr.Zero;
            }
        }
    }
}
