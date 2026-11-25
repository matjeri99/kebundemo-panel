/**
 * ============================================================
 * KebunDemo Backend v1.0 - Google Apps Script
 * ------------------------------------------------------------
 * Peranan : simpan data sensor, log peristiwa dan baris gilir arahan
 * Aliran  : Panel Web (GitHub Pages) <-> Apps Script <-> ESP32
 * Oleh    : SrieMatjeri / D77Tech
 *
 * Endpoint (semua GET, wajib ?key=KUNCI):
 *   action=poll    ESP32 hantar status + ambil arahan baharu
 *   action=log     ESP32 hantar peristiwa (pam hidup/mati, tetapan)
 *   action=status  Panel web baca status, sejarah dan log
 *   action=cmd     Panel web hantar arahan (siram/stop/auto_on/auto_off/ambang)
 *   POST           Borang tinjauan awam (borang.html), tanpa kunci, ke helaian Borang
 * ============================================================
 */

const TETAPAN = {
  SELA_LOG_MINIT: 5,        // simpan satu baris data setiap 5 minit
  HAD_TALIAN_SAAT: 60,      // ESP32 dikira luar talian jika senyap > 60 saat
  SEJARAH_LALAI: 288,       // 288 x 5 minit = 24 jam
  LUPUT_ARAHAN_MINIT: 5,    // arahan yang tidak diambil dalam 5 minit dibatalkan
  ARAHAN_SAH: ['siram', 'stop', 'auto_on', 'auto_off', 'ambang'],
};

const HELAIAN = {
  STATUS: ['Status', ['Masa', 'Kelembapan (%)', 'Raw', 'Pam', 'Mod Auto', 'Ambang (%)', 'Sensor OK', 'RSSI (dBm)', 'Uptime (s)']],
  DATA:   ['Data',   ['Masa', 'Kelembapan (%)', 'Raw', 'Pam', 'Mod Auto', 'Ambang (%)']],
  ARAHAN: ['Arahan', ['ID', 'Masa Dicipta', 'Arahan', 'Nilai', 'Status', 'Masa Dihantar', 'Sumber']],
  LOG:    ['Log',    ['Masa', 'Jenis', 'Mesej']],
};

/** Jalankan SEKALI dari editor: cipta helaian dan kunci API. */
function setup() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  Object.values(HELAIAN).forEach(([nama, tajuk]) => {
    const sh = ss.getSheetByName(nama) || ss.insertSheet(nama);
    sh.getRange(1, 1, 1, tajuk.length).setValues([tajuk]).setFontWeight('bold');
    sh.setFrozenRows(1);
  });
  const props = PropertiesService.getScriptProperties();
  if (!props.getProperty('KUNCI')) {
    props.setProperty('KUNCI', Utilities.getUuid().replace(/-/g, '').slice(0, 10));
  }
  Logger.log('KUNCI API anda: ' + props.getProperty('KUNCI'));
}

function doGet(e) {
  const p = (e && e.parameter) || {};
  const kunci = PropertiesService.getScriptProperties().getProperty('KUNCI');
  if (!kunci || p.key !== kunci) return keluar_({ ok: false, ralat: 'Kunci tidak sah' });
  try {
    switch (p.action) {
      case 'poll':   return keluar_(poll_(p));
      case 'log':    return keluar_(logPeristiwa_(p));
      case 'status': return keluar_(status_(p));
      case 'cmd':    return keluar_(tambahArahan_(p));
      default:       return keluar_({ ok: false, ralat: 'Action tidak dikenali' });
    }
  } catch (err) {
    return keluar_({ ok: false, ralat: String(err) });
  }
}

// ---------------- Borang tinjauan awam (tanpa kunci API) ----------------
// Dihantar dari borang.html di GitHub Pages sebagai POST (text/plain, JSON).
// Setiap jawapan = satu baris dalam helaian "Borang". Lajur dicipta automatik.
function doPost(e) {
  try {
    const data = JSON.parse((e && e.postData && e.postData.contents) || '{}');
    if (data.jenis !== 'borang') return keluar_({ ok: false, ralat: 'Jenis tidak sah' });
    if (data.laman) return keluar_({ ok: true });  // perangkap spam (medan tersembunyi)
    const r = data.jawapan || {};
    const wajib = ['A1. Nama kebun', 'A4. Nama wakil', 'A6. No. telefon (WhatsApp)', 'C8. Status sistem pengairan / fertigasi'];
    if (wajib.some(k => !r[k] || String(r[k]).trim() === '')) return keluar_({ ok: false, ralat: 'Medan wajib tidak lengkap' });

    const lock = LockService.getScriptLock();
    lock.waitLock(10000);
    try {
      const ss = SpreadsheetApp.getActiveSpreadsheet();
      const sh = ss.getSheetByName('Borang') || ss.insertSheet('Borang');
      let tajuk = sh.getLastRow() > 0 ? sh.getRange(1, 1, 1, sh.getLastColumn()).getValues()[0] : ['Masa'];
      Object.keys(r).forEach(k => { if (!tajuk.includes(k)) tajuk.push(k); });
      sh.getRange(1, 1, 1, tajuk.length).setValues([tajuk]).setFontWeight('bold');
      sh.setFrozenRows(1);
      const baris = tajuk.map(k => {
        if (k === 'Masa') return new Date();
        const v = r[k];
        const teks = Array.isArray(v) ? v.join(', ') : (v === undefined ? '' : String(v));
        return teks.slice(0, 1000);
      });
      sh.appendRow(baris);
    } finally {
      lock.releaseLock();
    }
    return keluar_({ ok: true });
  } catch (err) {
    return keluar_({ ok: false, ralat: String(err) });
  }
}

// ---------------- Dari ESP32 ----------------
function poll_(p) {
  const kini = new Date();
  const baris = [
    kini, nombor_(p.k), nombor_(p.raw),
    p.pam === '1' ? 'HIDUP' : 'MATI',
    p.auto === '1' ? 'ON' : 'OFF',
    nombor_(p.ambang),
    p.sensor === '1' ? 'YA' : 'TIDAK',
    nombor_(p.rssi), nombor_(p.uptime),
  ];
  helaian_('STATUS').getRange(2, 1, 1, baris.length).setValues([baris]);

  const props = PropertiesService.getScriptProperties();
  const akhir = Number(props.getProperty('AKHIR_LOG') || 0);
  if (p.paksa === '1' || kini.getTime() - akhir >= TETAPAN.SELA_LOG_MINIT * 60000) {
    helaian_('DATA').appendRow(baris.slice(0, 6));
    props.setProperty('AKHIR_LOG', String(kini.getTime()));
  }
  return { ok: true, arahan: ambilArahan_(kini) };
}

function ambilArahan_(kini) {
  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    const sh = helaian_('ARAHAN');
    const akhir = sh.getLastRow();
    if (akhir < 2) return [];
    const mula = Math.max(2, akhir - 49);
    const data = sh.getRange(mula, 1, akhir - mula + 1, 7).getValues();
    const keluar = [];
    data.forEach((r, i) => {
      if (r[4] !== 'BARU') return;
      const umur = kini.getTime() - new Date(r[1]).getTime();
      if (umur > TETAPAN.LUPUT_ARAHAN_MINIT * 60000) {
        sh.getRange(mula + i, 5, 1, 2).setValues([['LUPUT', kini]]);
      } else if (keluar.length < 5) {
        keluar.push({ id: r[0], cmd: r[2], nilai: Number(r[3]) || 0 });
        sh.getRange(mula + i, 5, 1, 2).setValues([['DIHANTAR', kini]]);
      }
    });
    return keluar;
  } finally {
    lock.releaseLock();
  }
}

function logPeristiwa_(p) {
  helaian_('LOG').appendRow([new Date(), p.jenis || 'INFO', p.mesej || '']);
  return { ok: true };
}

// ---------------- Dari Panel Web ----------------
function tambahArahan_(p) {
  const cmd = String(p.cmd || '').toLowerCase();
  if (!TETAPAN.ARAHAN_SAH.includes(cmd)) return { ok: false, ralat: 'Arahan tidak sah' };
  let nilai = Number(p.nilai || 0);
  if (cmd === 'siram') nilai = Math.min(Math.max(nilai || 5, 1), 30);
  if (cmd === 'ambang' && !(nilai >= 10 && nilai <= 90)) return { ok: false, ralat: 'Ambang mesti 10 hingga 90' };

  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    const id = Utilities.getUuid().slice(0, 8);
    helaian_('ARAHAN').appendRow([id, new Date(), cmd, nilai, 'BARU', '', p.sumber || 'Panel Web']);
    return { ok: true, id: id };
  } finally {
    lock.releaseLock();
  }
}

function status_(p) {
  const kini = Date.now();
  const st = helaian_('STATUS').getRange(2, 1, 1, 9).getValues()[0];
  const masa = st[0] instanceof Date ? st[0].getTime() : 0;
  const status = {
    masa: masa, kelembapan: st[1], raw: st[2], pam: st[3], auto: st[4],
    ambang: st[5], sensor: st[6], rssi: st[7], uptime: st[8],
    dalamTalian: masa > 0 && (kini - masa) < TETAPAN.HAD_TALIAN_SAAT * 1000,
  };

  const bil = Math.min(Number(p.n) || TETAPAN.SEJARAH_LALAI, 2000);
  const shD = helaian_('DATA');
  const nD = shD.getLastRow();
  const sejarah = nD < 2 ? [] : shD
    .getRange(Math.max(2, nD - bil + 1), 1, Math.min(bil, nD - 1), 2).getValues()
    .map(r => [r[0] instanceof Date ? r[0].getTime() : 0, r[1]]);

  const shL = helaian_('LOG');
  const nL = shL.getLastRow();
  const log = nL < 2 ? [] : shL
    .getRange(Math.max(2, nL - 19), 1, Math.min(20, nL - 1), 3).getValues()
    .reverse()
    .map(r => ({ masa: r[0] instanceof Date ? r[0].getTime() : 0, jenis: r[1], mesej: r[2] }));

  const shA = helaian_('ARAHAN');
  const nA = shA.getLastRow();
  const menunggu = nA < 2 ? 0 : shA
    .getRange(Math.max(2, nA - 49), 5, Math.min(50, nA - 1), 1).getValues()
    .filter(r => r[0] === 'BARU').length;

  return { ok: true, status: status, sejarah: sejarah, log: log, menunggu: menunggu };
}

// ---------------- Bantu ----------------
function keluar_(o) {
  return ContentService.createTextOutput(JSON.stringify(o)).setMimeType(ContentService.MimeType.JSON);
}

function helaian_(k) {
  return SpreadsheetApp.getActiveSpreadsheet().getSheetByName(HELAIAN[k][0]);
}

function nombor_(v) {
  const n = Number(v);
  return v === undefined || v === '' || isNaN(n) ? '' : n;
}
