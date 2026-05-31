# TODO - Redesign VISUALISER L R DAN VOLUME

## Task: Desain ulang bagian kiri (L/R Visualizers + Volume) - anti-flickering

### Step 1: Analyse current code structure
- [x] Baca sewu_ui.c - COMPLETE
- [x] Baca sewu_app_state.h - COMPLETE

### Step 2: Rancang desain baru
- [x] Buat brainstorm plan - COMPLETE

### Step 3: Implementasi
- [x] 3.1 Redesain draw_input_viz_bar - bar vertikal yang berdiri dengan gradient dan peak hold - COMPLETE
- [x] 3.2 Tambahkan HYSTERESIS smoothing untuk nilai input (cepat naik, perlahan turun) - COMPLETE
- [x] 3.3 Tambahkan threshold-based redraw - COMPLETE  
- [x] 3.4 Peak Hold dengan decay lambat - COMPLETE
- [x] 3.5 Update Constants - COMPLETE

### Step 4: Testing
- [ ] Build dan flash ke device
- [ ] Test L/R visualizers - harus standing dan tidak flickering
- [ ] Test Volume display
- [ ] Feedback dari user

## Summary Perubahan:

### Design Visualizer:
1. Bar lebih lebar (24px dari 18px sebelumnya)
2. Spacing lebih besar (16px)
3. Tinggi bar lebih optimal
4. Gradient 3 zone: Hijau (bawah 40%), Amber (tengah 35%), Merah (atas 25%)
5. Border ganda untuk efek "berdiri" yang jelas

### Anti-Flickering Logic:
1. **HYSTERESIS:**
   - Cepat naik: langsung responsif 75% nilai baru
   - Perlahan turun: hanya 20% dari selisih per frame
   
2. **PEAK HOLD:**
   - Langsung naik ke peak baru
   - Decay lambat hanya 3% per frame
   
3. **THRESHOLD-BASED REDRAW:**
   - Hanya redraw jika perubahan > 2%

### Constants:
- VIZ_CHANGE_THRESHOLD = 2
- VIZ_SMOOTH_FACTOR = responsif
- Peak decay lambat
