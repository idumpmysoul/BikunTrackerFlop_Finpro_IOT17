# Proyek Akhir IoT: Sistem Pelacak Bus Kampus Real-time


Repositori ini berisi semua deliverables untuk proyek akhir IoT, termasuk kode sumber, dokumentasi, dan laporan. Proyek ini mengimplementasikan sistem pelacak bus kampus yang cerdas dan kontekstual menggunakan mikrokontroler ESP32, GPS, dan protokol MQTT.



#### GROUP 17 :
1. Abednego Zebua			    (2306161883)
2. Adhikananda Wira Januar		(2306267113)
3. Filaga Tifira Muthi			(2306208445)
4. Gerrardin Nabil Zulhian		(2306250661)

---

## 1. Introduction

### 1.1 Latar Belakang & Permasalahan

Sistem pelacak bus resmi yang ada saat ini di lingkungan kampus, meskipun fungsional, memiliki beberapa keterbatasan. Ketergantungan pada satu sistem terpusat membuatnya rentan terhadap *downtime*, yang dapat menyebabkan disinformasi bagi mahasiswa. Selain itu, informasi yang disajikan hanya tersedia melalui situs web, mengharuskan pengguna untuk aktif mencari informasi. Belum ada sistem informasi pasif yang dapat dilihat sekilas (*glanceable*) yang tersedia langsung di halte bus.

Proyek ini bertujuan untuk mengatasi masalah ini dengan mengembangkan sebuah sistem pelacak sekunder yang independen, tangguh, dan mampu menyajikan informasi yang relevan secara kontekstual langsung di lokasi halte.

### 1.2 Solusi yang Diusulkan

Solusi yang kami usulkan adalah sebuah ekosistem IoT yang terdiri dari dua komponen utama:
1.  **Unit Pelacak Bus:** Perangkat portabel berbasis ESP32 dan GPS yang dipasang di setiap bus. Unit ini secara otonom mengirimkan data lokasinya (setelah diubah menjadi nama zona) ke sebuah server perantara.
2.  **Unit Informasi Halte:** Perangkat stasioner berbasis ESP32 dan display LCD yang dipasang di setiap halte. Unit ini secara cerdas menerima data dari semua bus, memfilternya berdasarkan relevansi untuk lokasinya, dan menampilkan status bus yang mudah dipahami (misalnya, "Mendekat", "Telah Lewat", "Halte Berikutnya").

Komunikasi antara semua unit difasilitasi oleh protokol MQTT, menciptakan sistem yang skalabel dan *loosely coupled*.

### 1.3 Kriteria Keberhasilan
Proyek ini dianggap berhasil jika:
- Sistem mampu melacak minimal dua unit bus secara bersamaan dan menampilkan status keduanya di satu unit halte (untuk halte bersama).
- Unit Halte mampu secara cerdas menampilkan informasi yang relevan berdasarkan konfigurasinya (halte khusus atau bersama).
- Latensi pembaruan informasi di halte kurang dari 20 detik dalam kondisi jaringan ideal.
- Proyek berhasil mengimplementasikan minimal 6 modul praktikum, termasuk FreeRTOS pada Unit Bus.

---

## 2. Implementation

### 2.1 Desain Perangkat Keras

Sistem ini menggunakan dua jenis perangkat keras:

*   **Unit Bus:**
    *   Mikrokontroler: ESP32 Dev Kit
    *   Sensor: Modul GPS NEO-6M
    *   Input/Output: 1x Push Button, 2x LED (Merah & Biru), 3x Resistor
    *   Sumber Daya: Power Bank
    *   Tujuan: Melacak lokasi dan menyediakan antarmuka sederhana bagi pengemudi.

*   **Unit Halte:**
    *   Mikrokontroler: ESP32 Dev Kit
    *   Output: Display LCD I2C 16x2
    *   Sumber Daya: Adaptor Dinding USB
    *   Tujuan: Menampilkan informasi yang telah diproses kepada pengguna.

**(Skema rangkaian detail dapat ditemukan di dalam laporan proyek.)**

### 2.2 Pengembangan Perangkat Lunak

#### Arsitektur
Arsitektur perangkat lunak sistem ini bersifat asimetris:
- **Unit Bus:** Menggunakan **FreeRTOS** untuk menangani beberapa tugas secara bersamaan: akuisisi data GPS, manajemen input pengguna (push button dengan *debouncing* dan persistensi state), dan komunikasi MQTT.
- **Unit Halte:** Menggunakan loop sederhana (`loop()`) karena sifatnya yang reaktif, menunggu pesan MQTT masuk untuk kemudian memproses dan menampilkan data.

#### Algoritma
Kecerdasan utama sistem berada di Unit Halte.
1.  **Peta Rute:** Setiap unit halte menyimpan peta rute lengkap (Jalur Merah & Biru) sebagai array di dalam memorinya.
2.  **Filter Kontekstual:** Saat pesan MQTT diterima, unit pertama-tama memfilter berdasarkan `line_id` untuk memastikan pesan tersebut relevan dengan lokasinya (misalnya, halte khusus jalur Merah akan mengabaikan data dari bus jalur Biru).
3.  **Kalkulasi Status:** Dengan membandingkan indeks lokasi bus saat ini dengan indeks lokasinya sendiri di dalam array rute, unit dapat secara matematis menentukan apakah bus sedang mendekat, telah lewat, atau berada di halte. Informasi tambahan seperti "halte berikutnya" juga dihitung dari array ini.

### 2.3 Integrasi Sistem
Integrasi antara semua perangkat dilakukan secara nirkabel melalui **broker MQTT publik**. Setiap unit terhubung ke jaringan Wi-Fi secara independen dan berkomunikasi melalui topik MQTT yang telah ditentukan (`bikun/location`). Pendekatan ini memastikan bahwa penambahan unit baru (bus atau halte) tidak memerlukan perubahan pada unit yang sudah ada.

---

## 3. Testing and Evaluation

### 3.1 Skenario Pengujian
Pengujian dilakukan dalam beberapa tahap:
1.  **Sanity Check:** Sebuah skrip pengujian perangkat keras dijalankan di setiap unit untuk memverifikasi fungsionalitas semua komponen secara individual (Wi-Fi, MQTT, GPS, LCD, LED, Push Button).
2.  **Pengujian dengan Data Simulasi (Mock-up):** Untuk memungkinkan pengembangan paralel, Unit Bus diimplementasikan dengan mode simulasi. Mode ini memungkinkan bus untuk "bergerak" secara virtual di sepanjang rute yang telah ditentukan, mempublikasikan data lokasi palsu secara berkala. Hal ini memungkinkan logika Unit Halte untuk diuji secara menyeluruh dalam kondisi yang terkontrol.
3.  **Pengujian End-to-End:** Sistem diuji secara keseluruhan dengan satu Unit Bus yang berfungsi (menggunakan data GPS nyata) dan satu Unit Halte. Unit Bus dibawa berkeliling area kampus untuk memvalidasi akurasi pelacakan dan responsivitas tampilan di Unit Halte.

### 3.2 Hasil Pengujian
- **Sanity Check:** Semua komponen perangkat keras berhasil lulus pengujian, mengonfirmasi bahwa rangkaian fisik telah dirakit dengan benar.
- **Pengujian Simulasi:** Unit Halte berhasil menerima, memfilter, dan menampilkan data dari dua bus simulasi secara bersamaan dan akurat. Logika penentuan status "mendekat" dan "telah lewat" berfungsi sesuai harapan.
- **Pengujian End-to-End:** Sistem berhasil melacak pergerakan bus nyata. Pembaruan di layar LCD terjadi dengan latensi rata-rata 10-15 detik, memenuhi kriteria keberhasilan.

### 3.3 Evaluasi
Secara keseluruhan, sistem berfungsi sesuai dengan desain. Arsitektur berbasis MQTT terbukti andal dan responsif. Algoritma perbandingan indeks pada Unit Halte efektif dalam menyediakan informasi yang kontekstual dan berguna. Tantangan utama yang dihadapi adalah ketergantungan pada sinyal GPS yang stabil, yang dapat terganggu di area dengan banyak gedung tinggi.

---

## 4. Conclusion
Proyek ini berhasil mendemonstrasikan kelayakan pembangunan sistem pelacak bus kampus sekunder yang cerdas, tangguh, dan berbiaya rendah menggunakan teknologi IoT yang tersedia secara umum. Dengan mengimplementasikan logika kontekstual di tingkat halte, sistem ini mampu memberikan nilai tambah yang signifikan dibandingkan dengan sekadar menampilkan data lokasi mentah. Arsitektur yang dipilih terbukti skalabel dan mudah untuk dikembangkan lebih lanjut. Sebagai langkah selanjutnya, sistem ini dapat ditingkatkan dengan menggunakan server MQTT pribadi untuk keamanan yang lebih baik dan mengadopsi teknologi komunikasi LoRa untuk mengurangi ketergantungan pada Wi-Fi dan biaya operasional.

---

## 5. Documentation
Rangkaian : ![Foto Rangkaian](https://imgur.com/OnOt8Ug.jpg)
![Rangkaian](https://imgur.com/gNbLNf7.jpg)
Serial Monitor (Bus 2) : ![Foto Serial Monitor Bus 2](https://imgur.com/GdQYlt3.jpg)




## 6. References
- [1]“1.2 Introduction to RTOS | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/12-introduction-to-rtos
- [2]“7.1 Introduction: The ... | Digilab UI,” Digilabdte.com, 2025. https://learn.digilabdte.com/books/internet-of-things/page/71-introduction-the-iot-communication-stack‌
- [3]“ESP-IDF Programming Guide - ESP32 - — ESP-IDF Programming Guide latest documentation,” docs.espressif.com. https://docs.espressif.com/projects/esp-idf/en/latest/esp32/‌
- [4]“MQTT Version 3.1.1,” docs.oasis-open.org. https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
‌

---

## Hal yang Perlu Disesuaikan Sebelum Upload

### 1. SSID & Password Wi-Fi  
Pada setiap file `.ino`, ubah:

```cpp
const char* WIFI_SSID = "NAMA_WIFI_KAMU";
const char* WIFI_PASS = "PASSWORD_WIFI_KAMU";
````

---

### ️2. MQTT Broker (sudah siap)

```cpp
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "bikun/location";
```

---

### 3. Konfigurasi Unit Bus

Di `Bus.ino`:

```cpp
#define BUS_ID  "BUS_1"
#define DEFAULT_LINE "RED"
```

BUS_ID dan DEFAULT_LINE dapat diubah sesuai dengan setup Bus yang diinginkan

---

### 4. Konfigurasi Lokasi Unit Halte

Pada `Halte.ino`:

```cpp
#define MY_HALTE_ID "FT"
String RELEVANT_LINE = "RED";
```

Untuk memindahkan unit halte ke lokasi lain, ubah MY_HALTE_ID dan RELEVANT_LINE dengan menggunakan tabel berikut:

| Halte      | MY_HALTE_ID | RELEVANT_LINE |
| ---------- | ----------- | ------------- |
| FT         | `"FT"`      | `"RED"`       |
| FE         | `"FE"`      | `"RED"`       |
| FIB        | `"FIB"`     | `"RED"`       |
| STASIUN UI | `"STASIUN"` | `"BOTH"`      |
| ASRAMA     | `"ASRAMA"`  | `"BOTH"`      |
| FISIP      | `"FISIP"`   | `"BLUE"`      |
| FH         | `"FH"`      | `"BLUE"`      |
| FPsi       | `"FPsi"`    | `"BLUE"`      |

Ubah > Upload > Selesai

---

## Cara Menjalankan

### Unit Bus

1. Nyalakan ESP32 + GPS
2. Buka Serial Monitor
3. Tunggu GPS lock
4. Saat bergerak antar zona > status otomatis ter-update lewat MQTT

### Unit Halte

1. Nyalakan ESP32 + LCD
2. LCD akan menampilkan:

* “Approaching”
* “Arrived”
* “Passed”

Berdasarkan lokasi bus terhadap halte tersebut

---

---

## Troubleshooting

| Masalah                | Solusi                            |
| ---------------------- | --------------------------------- |
| LCD blank              | Ganti alamat I2C: `0x27` ➜ `0x3F` |
| GPS tidak lock satelit | Coba dekat jendela / outdoor      |
| Status tidak update    | Restart hotspot / cek MQTT WiFi   |

---

