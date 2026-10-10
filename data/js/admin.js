const $ = (id) => document.getElementById(id);

function fmtTime(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  return (h ? h + ':' : '') + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}
function fmtUptime(ms) {
  const s = Math.floor(ms / 1000);
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm ' + (s % 60) + 's';
}
function num(v, n) { return v === null || v === undefined ? '--' : Number(v).toFixed(n); }
function flag(label, on, cls) {
  const c = cls || (on ? 'bad' : 'ok');
  return '<span class="tagp ' + c + '">' + label + ' ' + (on ? 'YES' : 'NO') + '</span>';
}
function bars(values, scale, unit) {
  const max = Math.max(scale, ...values.map((v) => Math.abs(v)), 0.0001);
  return values.map((v) => {
    const h = Math.max(2, Math.abs(v) / max * 100);
    return '<div class="bar" style="height:' + h + '%" title="' + Number(v).toFixed(3) + unit + '"></div>';
  }).join('');
}
function renderAlerts(list) {
  const el = $('alerts');
  if (!list || !list.length) { el.innerHTML = '<li class="muted">No alerts yet</li>'; return; }
  el.innerHTML = list.map((x) => {
    const sev = x.sev >= 2 ? 'sev2' : (x.sev === 1 ? 'sev1' : '');
    const tag = x.sev >= 2 ? 'URGENT' : (x.sev === 1 ? 'WARN' : 'INFO');
    return '<li class="' + sev + '"><span class="t">' + fmtTime(x.t) + '</span><b>' + tag + '</b><span>' + x.msg + '</span></li>';
  }).join('');
}

async function tick() {
  try {
    const res = await fetch('/admin/data');
    if (!res.ok) throw 0;
    const d = await res.json();
    $('link').textContent = 'LINK OK';
    $('link').className = 'status ok';

    $('flags').innerHTML =
      flag('Overspeed', d.overspeed, d.overspeed ? 'bad' : 'ok') +
      flag('Shock', d.shock, d.shock ? 'bad' : 'ok') +
      flag('Impact', d.impact, d.impact ? 'bad' : 'ok') +
      flag('Sharp turn', d.sharpTurn, d.sharpTurn ? 'warn' : 'ok') +
      flag('Air rising', d.airQualityBad, d.airQualityBad ? 'warn' : 'ok') +
      flag('GPS fix', d.gpsValid, d.gpsValid ? 'ok' : 'warn');

    $('rax').textContent = num(d.ax, 4);
    $('ray').textContent = num(d.ay, 4);
    $('raz').textContent = num(d.az, 4);
    $('rtot').textContent = num(d.totalAcceleration, 4);
    $('rdyn').textContent = num(d.dynamicAcceleration, 4);
    $('rdavg').textContent = num(d.dynamicAverage, 4);

    $('rgx').textContent = num(d.gx, 3);
    $('rgy').textContent = num(d.gy, 3);
    $('rgz').textContent = num(d.gz, 3);
    $('rgyaw').textContent = num(d.yawAverage, 2);

    $('atemp').textContent = num(d.temperature, 2);
    $('ahum').textContent = num(d.humidity, 2);
    $('amqraw').textContent = d.mqRaw;
    $('amqv').textContent = num(d.mqVolt, 4);
    $('amqb').textContent = num(d.mqBaseline, 1);
    $('amqt').textContent = (d.mqBaselineReady ? (d.mqTrendPct >= 0 ? '+' : '') + num(d.mqTrendPct, 2) : 'warming');

    $('agps').textContent = d.gpsValid ? 'VALID' : 'no fix';
    $('alat').textContent = num(d.latitude, 6);
    $('alon').textContent = num(d.longitude, 6);
    $('aspd').textContent = num(d.speedKmph, 2);
    $('asat').textContent = d.satellites;

    $('aup').textContent = fmtUptime(d.uptimeMs);
    $('aheap').textContent = d.freeHeap;
    $('acli').textContent = d.wifiClients;
    $('asensors').textContent = Object.keys(d.sensors).filter((k) => d.sensors[k]).join(', ') || 'none';

    $('tspeed').textContent = num(d.thresholds.speed, 1) + ' km/h';
    $('tshock').textContent = num(d.thresholds.shock, 2) + ' / ' + num(d.thresholds.impact, 2) + ' g';
    $('tturn').textContent = num(d.thresholds.turn, 0) + ' deg/s';
    $('ttemp').textContent = num(d.thresholds.tempWarn, 0) + ' / ' + num(d.thresholds.tempUrgent, 0) + ' C';
    $('tmq').textContent = num(d.thresholds.mqPct, 0) + ' %';

    $('dynbars').innerHTML = bars(d.dynWindow, d.thresholds.impact, 'g');
    $('yawbars').innerHTML = bars(d.gzWindow, d.thresholds.turn, 'deg/s');

    $('routeCount').textContent = d.routeCount;
    const rt = d.route || [];
    $('route').innerHTML = rt.length
      ? rt.slice(-8).reverse().map((x) => '<li><span class="t">' + fmtTime(x.t) + '</span><b>' + (x.label || '') + '</b><span>' +
        Number(x.lat).toFixed(5) + ', ' + Number(x.lon).toFixed(5) + '</span></li>').join('')
      : '<li class="muted">No waypoints yet</li>';

    renderAlerts(d.alerts);
  } catch (e) {
    $('link').textContent = 'LINK LOST';
    $('link').className = 'status bad';
  }
}

setInterval(tick, 1000);
tick();
