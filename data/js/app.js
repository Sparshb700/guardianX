const $ = (id) => document.getElementById(id);

let audioCtx = null;
let pendingBuzz = false;
let lastHighAlert = false;

function makeAudio() {
  if (audioCtx) return audioCtx;
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) return null;
  audioCtx = new AC();
  return audioCtx;
}

function playBuzz() {
  const ctx = makeAudio();
  if (!ctx) return;
  if (ctx.state !== 'running') { pendingBuzz = true; return; }
  pendingBuzz = false;
  const now = ctx.currentTime;
  for (let i = 0; i < 3; i++) {
    const t = now + i * 0.22;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = 'square';
    osc.frequency.setValueAtTime(i === 1 ? 540 : 780, t);
    gain.gain.setValueAtTime(0.0001, t);
    gain.gain.exponentialRampToValueAtTime(0.18, t + 0.015);
    gain.gain.exponentialRampToValueAtTime(0.0001, t + 0.17);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(t);
    osc.stop(t + 0.18);
  }
}

function unlockAudio() {
  const ctx = makeAudio();
  if (!ctx) return;
  ctx.resume().then(function () {
    if (pendingBuzz) playBuzz();
  }).catch(function () {});
}

['pointerdown', 'touchstart', 'keydown'].forEach(function (evt) {
  window.addEventListener(evt, unlockAudio, { passive: true });
});

function fmtTime(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  return (h ? h + ':' : '') + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}

function tempPct(t) {
  return Math.max(0, Math.min(100, (t / 50) * 100));
}

function tempReading(t, d) {
  if (t === null || t === undefined) return { label: 'No reading', cls: 'muted', fill: '', pct: 0 };
  const pct = tempPct(t);
  if (t < d.tempTooCold) return { label: 'Too cold', cls: 'bad', fill: 'bad', pct };
  if (t < d.tempCold) return { label: 'Cold', cls: 'warn', fill: 'warn', pct };
  if (t <= d.tempHot) return { label: 'Normal', cls: 'ok', fill: 'ok', pct };
  if (t < d.tempTooHot) return { label: 'Hot', cls: 'warn', fill: 'warn', pct };
  return { label: 'Too hot', cls: 'bad', fill: 'bad', pct };
}

function comfortReading(h) {
  if (h === null || h === undefined) return { label: 'No reading', cls: 'muted', sub: '' };
  if (h < 30) return { label: 'Dry', cls: 'warn', sub: Number(h).toFixed(0) + '% humidity' };
  if (h <= 60) return { label: 'Moderate', cls: 'ok', sub: Number(h).toFixed(0) + '% humidity' };
  return { label: 'Humid', cls: 'warn', sub: Number(h).toFixed(0) + '% humidity' };
}

function setReading(id, label, cls, subId, sub) {
  const el = $(id);
  el.textContent = label;
  el.className = 'v ' + cls;
  if (subId) $(subId).textContent = sub || '';
}

function renderAlerts(list) {
  const el = $('alerts');
  if (!list || !list.length) { el.innerHTML = '<li class="muted">Nothing yet</li>'; return; }
  el.innerHTML = list.map((x) => {
    const sev = x.sev >= 2 ? 'sev2' : (x.sev === 1 ? 'sev1' : '');
    return '<li class="' + sev + '"><span class="t">' + fmtTime(x.t) + '</span><span>' + x.msg + '</span></li>';
  }).join('');
}

async function tick() {
  try {
    const res = await fetch('/data', { cache: 'no-store' });
    if (!res.ok) throw 0;
    const d = await res.json();
    $('link').textContent = 'Connected';
    $('link').className = 'status ok';

    const highAlert = !!(d.crash || d.flipped || d.smoking);
    const banner = $('highAlert');
    if (banner) banner.hidden = !highAlert;
    if (highAlert && !lastHighAlert) playBuzz();
    lastHighAlert = highAlert;

    const problems = d.problems || [];
    const temp = tempReading(d.temperature, d);
    const comfort = comfortReading(d.humidity);

    if (d.homeSafe) {
      setReading('overall', 'Reached home safely', 'ok', 'overallSub', 'You confirmed your child reached home safely.');
      $('arrival').textContent = 'Home arrival confirmed by you.';
      $('arrival').className = 'arrival ok';
    } else if (d.sentBack) {
      if (d.problem) {
        setReading('overall', 'Attention needed', 'bad', 'overallSub', problems.join(' • '));
      } else {
        setReading('overall', 'On the way home', 'ok', 'overallSub', 'The school has sent your child back.');
      }
      $('arrival').textContent = 'Your child has been sent back from school.';
      $('arrival').className = 'arrival warn';
    } else if (d.arrived) {
      setReading('overall', 'Reached school safely', 'ok', 'overallSub', 'Confirmed by the teacher.');
      $('arrival').textContent = 'School arrival confirmed by the teacher.';
      $('arrival').className = 'arrival ok';
    } else if (d.problem) {
      setReading('overall', 'Attention needed', 'bad', 'overallSub', problems.join(' • '));
      $('arrival').textContent = 'Waiting for the school to confirm arrival.';
      $('arrival').className = 'arrival warn';
    } else {
      setReading('overall', 'Everything looks good', 'ok', 'overallSub', 'Your child is safe on the way to school.');
      $('arrival').textContent = 'Waiting for the school to confirm arrival.';
      $('arrival').className = 'arrival muted';
    }

    const homeCard = $('homeCard');
    if (d.sentBack) {
      homeCard.hidden = false;
      if (d.homeSafe) {
        $('homeStatus').textContent = 'Home arrival confirmed by you.';
        $('homeArrive').textContent = 'Home arrival confirmed';
        $('homeArrive').disabled = true;
      } else {
        $('homeStatus').textContent = 'Your child has been sent back from school. Please confirm when they reach home.';
        $('homeArrive').textContent = 'Confirm my child reached home safely';
        $('homeArrive').disabled = false;
      }
    } else {
      homeCard.hidden = true;
    }

    $('temp').textContent = temp.label;
    $('temp').className = 'v ' + temp.cls;
    $('tempval').textContent = d.temperature === null ? '--' : Number(d.temperature).toFixed(1) + '°C';
    const fill = $('tempfill');
    fill.style.width = temp.pct + '%';
    fill.className = 'tempfill ' + temp.fill;

    if (!d.smokingKnown) {
      setReading('smoke', 'Checking...', 'muted', 'smokeSub', 'The smoke sensor is warming up.');
    } else if (d.smoking) {
      setReading('smoke', 'Yes', 'bad', 'smokeSub', 'Smoke detected in the car.');
    } else {
      setReading('smoke', 'No', 'ok', 'smokeSub', 'No smoke detected.');
    }

    if (d.problem) {
      setReading('problem', 'Yes', 'bad', 'problemSub', problems.join(' • '));
    } else {
      setReading('problem', 'No', 'ok', 'problemSub', 'No problems detected.');
    }

    if (d.gpsValid) {
      setReading('speed', d.overspeed ? 'Too fast' : 'Normal', d.overspeed ? 'bad' : 'ok', 'speedSub', Number(d.speedKmph).toFixed(0) + ' km/h');
      setReading('location', 'Being tracked', 'ok', '', '');
      const lat = Number(d.latitude), lon = Number(d.longitude);
      $('locationSub').textContent = Math.abs(lat).toFixed(5) + (lat >= 0 ? ' N' : ' S') +
        ', ' + Math.abs(lon).toFixed(5) + (lon >= 0 ? ' E' : ' W');
    } else {
      setReading('speed', 'No reading', 'muted', 'speedSub', 'Waiting for the vehicle signal.');
      setReading('location', 'Not available', 'muted', '', '');
      $('locationSub').textContent = 'Waiting for the location signal.';
    }

    setReading('comfort', comfort.label, comfort.cls, 'comfortSub', comfort.sub);
    renderAlerts(d.alerts);
  } catch (e) {
    $('link').textContent = 'Connection lost';
    $('link').className = 'status bad';
  }
}

async function confirmHome() {
  const btn = $('homeArrive');
  btn.disabled = true;
  btn.textContent = 'Confirming...';
  try {
    await fetch('/parent/home', { cache: 'no-store' });
  } catch (e) {}
  await tick();
}

async function resetTrip() {
  if (!confirm('Reset the arrival and sent-back status?')) return;
  const btn = $('resetTrip');
  btn.disabled = true;
  btn.textContent = 'Resetting...';
  try {
    await fetch('/parent/reset', { cache: 'no-store' });
  } catch (e) {}
  await tick();
  btn.disabled = false;
  btn.textContent = 'RESET';
}

$('homeArrive').addEventListener('click', confirmHome);
$('resetTrip').addEventListener('click', resetTrip);
setInterval(tick, 500);
tick();
