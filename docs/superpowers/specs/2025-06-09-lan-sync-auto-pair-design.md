# LAN Sync Auto-Pair Design

## Problem

Saat ini pairing antar device PPSSPP LAN Sync membutuhkan input manual IP:Port dan PIN — sangat tidak praktis karena IP berubah-ubah dan PIN harus diketik manual.

## Goal

Pairing cukup **tap nama device** di daftar → ter-pair. PIN validation tetap ada di backend (auto-exchanged). IP tidak perlu diketik manual.

## Existing Patterns

| Pattern | Contoh | Penggunaan |
|---------|--------|------------|
| `System_Toast` | `System_Toast("Paired!")` | Notifikasi non-blocking |
| `screenManager()->push()` | `screenManager()->push(new DisplayLayoutScreen(...))` | Navigasi ke screen baru |
| `PopupScreen` + `CreatePopupContents()` | Custom screens | Screen dialog dengan daftar peer |
| `OnClick.Add` callback | `choice->OnClick.Add([...](EventParams &){...})` | Tombol aksi |
| Auto-refresh polling | `lastPeerRefresh_` tiap 5s (SDL ImGui) | Refresh daftar peer periodik |

## Flow

### Initiation (Device A)
1. User tap `[Pair]` di samping peer B pada daftar discovered peers
2. Device A kirim `POST /api/v1/pair-request` ke Device B
3. Device A terima response `{"status":"pending","requestId":"req-123"}`
4. Device A show toast "Pair request sent to Device B"
5. Device A mulai polling `GET /api/v1/pair-status?requestId=req-123` tiap 3 detik (max 30s timeout)

### Reception (Device B)
1. HTTP handler terima `POST /api/v1/pair-request`
2. Simpan pending request di memory: `{requestId, peerId, peerName, device, host, port, timestamp}`
3. Return `{"status":"pending","requestId":"req-123"}`
4. Show `System_Toast("Pair request from Device A")`
5. User buka Settings → Networking → Pair New Device
6. `LANPeerListScreen` muncul — lihat section "Pending Requests"
7. User tap `[Accept]` → `POST /api/v1/pair-respond` internal
8. Backend generate PIN via `GeneratePairingPin()`, auto-validate, simpan paired peer
9. Kembali ke poll response: "approved" dengan token

### Completion
1. Device A terima "approved" dari poll
2. Device A simpan Device B sebagai paired peer
3. Toast "Paired with MyPC!"
4. Peer muncul di section "Paired Devices" (sync-ready)

## API Endpoints

### `POST /api/v1/pair-request`
Request:
```json
{"id":"uuid-a", "name":"Pixel7", "device":"Android"}
```
Response:
```json
{"status":"pending", "requestId":"req-abc123"}
```

### `POST /api/v1/pair-respond`
Request:
```json
{"requestId":"req-abc123", "accept":true}
```
Response:
```json
{"status":"approved", "token":"sess-token", "peerId":"device-b-uuid"}
```
Atau:
```json
{"status":"rejected"}
```

### `GET /api/v1/pair-status?requestId=req-abc123`
Response:
```json
{"status":"approved"}
```
Atau: `"pending"`, `"rejected"`, `"expired"`

## UI: LANPeerListScreen

Extends `UI::PopupScreen`.

Layout:
```
+----------------------------------+
| Pair New Device           [Ref] |
+----------------------------------+
| Pending Requests:                |
| +------------------------------+ |
| | Pixel7 wants to pair       | |
| | Requested 10s ago          | |
| |                   [Accept] [X]| |  <- only on receiving side
| +------------------------------+ |
|                                   |
| Discovered Peers:                 |
| +------------------------------+ |
| | MyPC (Linux)          [Pair] | |  <- tap to initiate
| | 192.168.1.23                 | |
| +------------------------------+ |
|                                   |
| --- or enter manually ---        |
| [IP:Port______] [Pair]           |  <- fallback
+----------------------------------+
```

## Data Structures

### PendingPairRequest
```cpp
struct PendingPairRequest {
    std::string requestId;
    std::string peerId;
    std::string peerName;
    std::string device;
    std::string host;
    int port = 0;
    std::string pin;      // Generated PIN (for auto-validation)
    double timestamp = 0; // time_now_d()
    bool accepted = false;
    bool rejected = false;
};
```

## Files Changed

| File | Aksi |
|------|------|
| `UI/LANPeerListScreen.h` | **Baru** — header PopupScreen |
| `UI/LANPeerListScreen.cpp` | **Baru** — implementasi UI peer list |
| `Core/SaveStateLANSync.h` | Modif — tambah handler + pending request storage |
| `Core/SaveStateLANSync.cpp` | Modif — tambah 3 endpoint + pending request logic |
| `UI/GameSettingsScreen.cpp` | Modif — ganti Pair handler jadi push LANPeerListScreen |
| `CMakeLists.txt` | Modif — tambah 2 file baru |

## Security

- PIN tetap digunakan sebagai validasi (auto-generated, auto-validated)
- Pair request hanya diterima dari discovered peers di LAN (tidak bisa dari luar)
- Session token digunakan untuk autentikasi request sync selanjutnya
- Pending request expired setelah 60 detik
