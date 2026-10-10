const $ = (id) => document.getElementById(id);

function fmtTime(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  return (h ? h + ':' : '') + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}

function renderAlerts(list) {
  const el = $('alerts');
  if (!list || !list.length) { el.innerHTML = '<li class="muted">No alerts yet</li>'; return; }
  el.innerHTML = list.map((x) => {
    const sev = x.sev >= 2 ? 'sev2' : (x.sev === 1 ? 'sev1' : '');
    return '<li class="' + sev + '"><span class="t">' + fmtTime(x.t) + '</span><span>' + x.msg + '</span></li>';
  }).join('');
}

async function tick() {
  try {
    const res = await fetch('/data');
    if (!res.ok) throw 0;
    const d = await res.json();
    $('link').textContent = 'LINK OK';
    $('link').className = 'status ok';

    $('temp').textContent = d.temperature === null ? '--' : Number(d.temperature).toFixed(1);
    $('hum').textContent = d.humidity === null ? '--' : Number(d.humidity).toFixed(1);
    $('ax').textContent = Number(d.ax).toFixed(3);
    $('gx').textContent = Number(d.gx).toFixed(2);
    $('dyn').textContent = Number(d.dynamicAcceleration).toFixed(3);
    $('speed').textContent = d.gpsValid ? Number(d.speedKmph).toFixed(1) : '--';

    const aq = d.mqBaselineReady
      ? (d.airQualityBad ? '<span class="tagp bad">RISING</span>' : '<span class="tagp ok">NORMAL</span>')
      : '<span class="tagp warn">WARMING UP</span>';
    $('air').innerHTML = Number(d.mqVolt).toFixed(2) + ' V ' + aq;
    $('airsub').textContent = d.mqBaselineReady
      ? 'trend ' + (d.mqTrendPct >= 0 ? '+' : '') + Number(d.mqTrendPct).toFixed(1) + '% vs baseline'
      : 'building baseline';

    $('motion').textContent = d.motion;
    $('motion').className = 'v ' + (d.shock ? 'bad' : 'ok');

    const st = [];
    st.push(d.overspeed ? '<span class="bad">OVERSPEED</span>' : '<span class="ok">speed OK</span>');
    st.push(d.shock ? '<span class="bad">HARSH / IMPACT</span>' : '<span class="ok">driving smooth</span>');
    st.push(d.sharpTurn ? '<span class="warn">SHARP TURN</span>' : '<span class="ok">no sharp turn</span>');
    $('trip').innerHTML = 'Speed: ' + (d.gpsValid ? Number(d.speedKmph).toFixed(1) + ' km/h' : 'no fix') +
      ' | ' + st.join(' | ') + ' | sustained avg ' + Number(d.dynamicAverage).toFixed(3) + ' g';

    $('loc').textContent = d.gpsValid
      ? Number(d.latitude).toFixed(6) + ', ' + Number(d.longitude).toFixed(6) + ' | satellites ' + d.satellites
      : 'Waiting for GPS fix';

    renderAlerts(d.alerts);
  } catch (e) {
    $('link').textContent = 'LINK LOST';
    $('link').className = 'status bad';
  }
}

setInterval(tick, 500);
tick();
