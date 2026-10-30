$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NorthCredentialStore {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct Credential {
        public UInt32 Flags, Type;
        public string TargetName, Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public UInt32 CredentialBlobSize;
        public IntPtr CredentialBlob;
        public UInt32 Persist, AttributeCount;
        public IntPtr Attributes;
        public string TargetAlias, UserName;
    }
    [DllImport("advapi32.dll", EntryPoint="CredReadW", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool ReadNative(string target, UInt32 type, UInt32 flags, out IntPtr credential);
    [DllImport("advapi32.dll", EntryPoint="CredWriteW", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool WriteNative(ref Credential credential, UInt32 flags);
    [DllImport("advapi32.dll", EntryPoint="CredDeleteW", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool DeleteNative(string target, UInt32 type, UInt32 flags);
    [DllImport("advapi32.dll")] static extern void CredFree(IntPtr credential);
    public static byte[] Read(string target) {
        IntPtr pointer;
        if (!ReadNative(target, 1, 0, out pointer)) {
            if (Marshal.GetLastWin32Error() == 1168) return new byte[0];
            throw new InvalidOperationException("Credential read failed");
        }
        try {
            Credential value = (Credential)Marshal.PtrToStructure(pointer, typeof(Credential));
            byte[] bytes = new byte[value.CredentialBlobSize];
            Marshal.Copy(value.CredentialBlob, bytes, 0, bytes.Length);
            return bytes;
        } finally { CredFree(pointer); }
    }
    public static void Write(string target, byte[] bytes) {
        if (bytes.Length > 2560) throw new InvalidOperationException("Credential too large");
        IntPtr pointer = Marshal.AllocHGlobal(bytes.Length);
        try {
            Marshal.Copy(bytes, 0, pointer, bytes.Length);
            Credential value = new Credential();
            value.Type = 1; value.TargetName = target; value.UserName = "qtNorth";
            value.Persist = 2; value.CredentialBlobSize = (UInt32)bytes.Length; value.CredentialBlob = pointer;
            if (!WriteNative(ref value, 0)) throw new InvalidOperationException("Credential write failed");
        } finally {
            for (int i = 0; i < bytes.Length; ++i) Marshal.WriteByte(pointer, i, 0);
            Marshal.FreeHGlobal(pointer);
        }
    }
    public static void Erase(string target) {
        if (!DeleteNative(target, 1, 0) && Marshal.GetLastWin32Error() != 1168)
            throw new InvalidOperationException("Credential delete failed");
    }
}
'@
try {
    $request = [Console]::In.ReadToEnd() | ConvertFrom-Json
    if (-not $request.key.StartsWith('qtNorth:session:')) { throw 'Invalid key' }
    $result = ''
    switch ($request.operation) {
        'read' { $result = [Convert]::ToBase64String([NorthCredentialStore]::Read($request.key)) }
        'write' { [NorthCredentialStore]::Write($request.key, [Convert]::FromBase64String($request.data)) }
        'erase' { [NorthCredentialStore]::Erase($request.key) }
        default { throw 'Invalid operation' }
    }
    [Console]::Write((@{ ok = $true; data = $result } | ConvertTo-Json -Compress))
} catch {
    # Do not include request contents or credential bytes in errors.
    [Console]::Write('{"ok":false}')
    exit 1
}
