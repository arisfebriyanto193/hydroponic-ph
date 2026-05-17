# Cara Ganti Akun Expo / EAS Build

Buka terminal di folder project:

G:\projek\joki\hydroponic-ph\Hidroponik

## 1. Logout akun lama

```bash
expo logout
```

atau:

```bash
eas logout
```

## 2. Login akun baru

```bash
expo login
```

atau:

```bash
eas login
```

## 3. Cek akun aktif

```bash
expo whoami
```

atau:

```bash
eas whoami
```

## 4. Jika masih nyangkut akun lama

Hapus folder:

```bash
C:\Users\USERNAME\.expo
```

Dan:

```bash
C:\Users\USERNAME\.eas
```

Lalu login ulang.

## 5. Cek app.json

Pastikan owner sesuai akun baru:

```json
{
  "expo": {
    "owner": "akunBaru"
  }
}
```

## 6. Build ulang

```bash
eas build -p android
```

atau:

```bash
npx expo start
```

