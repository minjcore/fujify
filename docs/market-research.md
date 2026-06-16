# Nghiên cứu thị trường — Fuji-fy

Định vị **Fuji-fy** (editor ảnh đẹp; free core, doanh thu từ **Storage + Ads + Premium**)
trong thị trường phần mềm chỉnh ảnh 2026. Số liệu có nguồn (xem §8). Cập nhật: 2026-06-16.

> Liên quan: [product-model.md](product-model.md), [software-design.md](software-design.md),
> [raw-editors-analysis.md](raw-editors-analysis.md) (phân tích cộng đồng OSS RAW).

---

## 1. Quy mô thị trường

| Phân khúc | 2026 | Dự báo | CAGR |
|---|---|---|---|
| Photo editing **software** (desktop+) | ~**$2.5 tỷ** | $3.5 tỷ (2033) | ~5.0% |
| Photo editing **app** (mobile) | ~**$1.85 tỷ** | $3.92 tỷ (2035) | ~8.7% |

Động lực tăng: **AI** trong editing, **mobile-first**, và **mạng xã hội** (>4 tỷ người dùng
chia sẻ ảnh hàng ngày). Mobile tăng nhanh gấp ~1.7× desktop ⇒ chiến lược **mobile-first** của
Fuji-fy ([mobile-first-start.md](mobile-first-start.md)) đi đúng xu hướng.

---

## 2. Phân khúc & bản đồ đối thủ

**A. RAW editor desktop (pro):** Adobe Lightroom, Capture One, DxO PhotoLab.
**B. OSS RAW editor:** RawTherapee, Darktable, RapidRAW, Filmvert — xem
[raw-editors-analysis.md](raw-editors-analysis.md).
**C. Mobile/social editor:** VSCO, PicsArt, Snapseed, Lightroom Mobile.
**D. Cloud ảnh (kề bên — đối thủ Storage):** Google Photos, iCloud+, Adobe Cloud.

Fuji-fy nằm ở **giao của B–C–D**: chất lượng RAW + đầu ra social + storage là trục thu phí.

---

## 3. Bảng giá đối thủ (2026)

| Sản phẩm | Mô hình | Giá | Ghi chú |
|---|---|---|---|
| **Lightroom** (Photography Plan) | Subscription | ~**$120/năm** (kèm 1TB: $19.99/th) | Không có perpetual |
| **Capture One Pro** | Sub hoặc perpetual | từ **$179/năm** · perpetual **$299** | Upgrade tính phí |
| **DxO PhotoLab 9 Elite** | Perpetual | **$240** (upgrade $120) | Mua đứt |
| **VSCO+** | Freemium | **$29.99/năm** | Free: 15 preset + basic |
| **PicsArt** | Freemium | Premium (free giới hạn ~3 save/ngày) | Ép subscribe |
| **Snapseed** | **Free hoàn toàn** | $0, không ads, không IAP | Của Google; chuẩn so sánh "free" |

Khoảng trống: phân khúc pro (LR/C1/DxO) đắt; mobile freemium (VSCO/PicsArt) khoá tính năng;
Snapseed free nhưng **không có cloud/đồng bộ**. Fuji-fy chen vào: **tool free, thu ở storage**.

---

## 4. Benchmark giá Cloud Storage (trục doanh thu chính)

| Dịch vụ | 50–100GB | 200GB | 2TB | ~Giá/TB/tháng |
|---|---|---|---|---|
| **Google One/Photos** | $1.99 (100GB) | $2.99 | $9.99 | ~$5/TB |
| **iCloud+** | $0.99 (50GB) | $2.99 | $9.99 | ~$5/TB |
| **Adobe** (Photography, 1TB kèm app) | — | — | — | ~$20/TB (bundle) |

Hệ quả cho Fuji-fy: giá storage thuần đã bị kéo về **~$5/TB**. Khó cạnh tranh nếu bán dung
lượng trần ⇒ phải **bán giá trị quanh storage** (proxy thông minh, "tráo hình"/pHash trong
[storage-swap-vision.md](storage-swap-vision.md), backup bản gốc + EXIF, đồng bộ preset),
không phải GB thô.

---

## 5. Định vị Fuji-fy

> **"Editor ảnh đẹp, free; trả tiền khi muốn giữ bản gốc RAW trên cloud."**

- **Khác LR/C1/DxO:** không bắt trả tiền để *chỉnh* — bỏ rào cản lớn nhất của phân khúc pro.
- **Khác VSCO/PicsArt:** không khoá tool/giới hạn save; doanh thu dịch sang storage + ads.
- **Khác Snapseed:** có **cloud/đồng bộ + đầu ra social chuẩn ≥2K** mà Snapseed thiếu.
- **Khác Google Photos/iCloud:** không chỉ là kho — là **editor RAW chất lượng** tích hợp.

**Mô hình kiếm tiền** (xem product-model): free core → **Ads** (free tier) → **Premium tắt
ads + quota storage**. Khớp chuẩn freemium VSCO/PicsArt nhưng đặt paywall ở chỗ ít gây khó
chịu hơn (storage thay vì tool).

---

## 6. Khách hàng mục tiêu

1. **Người chụp Sony/RAW phổ thông** (đúng gốc NEX-5N) — muốn ảnh đẹp nhanh, ngại LR đắt/nặng.
2. **Creator IG/Threads** — cần đầu ra đa tỉ lệ (story/feed/square) chất lượng cao + EXIF tag.
3. **Người ngại subscription** — chấp nhận ads hoặc trả nhỏ cho storage.

---

## 7. SWOT & rủi ro

**Strengths:** core free (chống lại LR/VSCO); engine dùng chung; đầu ra social chuẩn cao.
**Weaknesses:** POC chưa có catalog/AI mask/non-destructive (so RapidRAW/LR); brand mới.
**Opportunities:** mobile CAGR 8.7%; xu hướng "thoát Adobe"; AI editing.
**Threats:** Google Photos/iCloud kéo giá storage về ~$5/TB; Snapseed free; ads làm phiền dễ
gây bỏ app.

**Khuyến nghị:**
- Đừng bán GB thô — bán **giá trị quanh storage** (proxy, backup gốc, đồng bộ preset, pHash).
- Quảng cáo phải nhẹ; **Premium tắt ads** là đòn bẩy chuyển đổi chính.
- Giữ lời hứa "tool free" làm khác biệt cốt lõi với LR/VSCO.
- Ưu tiên **mobile-first** (CAGR cao hơn) nhưng dùng chung engine với desktop Studio.

---

## 8. Nguồn

- Photo editing software market: [Coherent Market Insights](https://www.coherentmarketinsights.com/industry-reports/photo-editing-software-market), [Business Research Insights](https://www.businessresearchinsights.com/market-reports/photo-editing-software-market-103286)
- Photo editing app (mobile) market: [Market Growth Reports](https://www.marketgrowthreports.com/market-reports/photo-editing-app-market-110283), [MarkWide Research](https://markwideresearch.com/global-photo-editing-app-market)
- Giá LR/C1/DxO: [Capture One pricing](https://hamsterstack.com/pricing/capture-one/), [Lightroom pricing](https://toolradar.com/tools/adobe-lightroom/pricing), [Fstoppers — break from Adobe](https://fstoppers.com/software/how-break-adobe-2026-subscription-free-creative-suite-719259)
- Cloud storage: [Google Photos pricing](https://apispine.com/google-photos/pricing), [Cloud storage 2026](https://www.soft-amis.com/blog/2026-03-14-cloud-storage-pricing-2026-best-deals/), [Adobe storage](https://josephnilo.com/blog/adobe-cloud-storage-pricing-explained/)
- Mobile freemium: [VSCO pricing](https://checkthat.ai/brands/vsco/pricing), [Best photo apps 2026](https://www.analyticsinsight.net/apps/best-mobile-photo-editing-apps-in-2026-for-android-and-ios)
