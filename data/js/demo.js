(function () {
  const $ = (id) => document.getElementById(id);

  const presets = {
    safe: { temp: 24, hum: 48, speed: 32, gps: true, smoke: false, bump: false, turn: false, arrived: false },
    problem: { temp: 34, hum: 65, speed: 67, gps: true, smoke: true, bump: true, turn: true, arrived: false },
    hot: { temp: 42, hum: 25, speed: 0, gps: true, smoke: false, bump: false, turn: false, arrived: false },
    arrived: { temp: 22, hum: 50, speed: 0, gps: true, smoke: false, bump: false, turn: false, arrived: true }
  };

  function setValue(id, value) {
    const el = $(id);
    if (el.type === 'checkbox') el.checked = value;
    else el.value = value;
  }

  function syncOutputs() {
    $('tempOut').textContent = Number($('tempIn').value).toFixed(1) + '\u00b0C';
    $('humOut').textContent = Number($('humIn').value).toFixed(0) + '%';
    $('speedOut').textContent = Number($('speedIn').value).toFixed(0) + ' km/h';
  }

  function applyPreset(name) {
    const p = presets[name];
    if (!p) return;
    setValue('tempIn', p.temp);
    setValue('humIn', p.hum);
    setValue('speedIn', p.speed);
    setValue('gpsIn', p.gps);
    setValue('smokeIn', p.smoke);
    setValue('bumpIn', p.bump);
    setValue('turnIn', p.turn);
    setValue('arrivedIn', p.arrived);
    syncOutputs();
  }

  function buildData() {
    const temp = Number($('tempIn').value);
    const hum = Number($('humIn').value);
    const speed = Number($('speedIn').value);
    const gps = $('gpsIn').checked;
    const smoke = $('smokeIn').checked;
    const bump = $('bumpIn').checked;
    const turn = $('turnIn').checked;
    const arrived = $('arrivedIn').checked;

    const problems = [];
    if (bump) problems.push('Sudden bump or hard impact');
    if (speed > 50) problems.push('Vehicle is going too fast');
    if (turn) problems.push('Sharp turn taken');
    if (smoke) problems.push('Smoke detected in the car');
    if (temp >= 38) problems.push('Car is too hot');
    else if (temp >= 30) problems.push('Car is hot');
    if (temp <= 8) problems.push('Car is too cold');
    else if (temp < 16) problems.push('Car is cold');

    const alerts = problems.map((msg, i) => ({
      t: 60 + i * 85,
      sev: /impact|too hot|too fast/.test(msg) ? 2 : 1,
      msg: msg
    }));
    if (arrived) alerts.push({ t: 900, sev: 0, msg: 'Reached school safely' });

    return {
      temperature: temp,
      humidity: hum,
      tempTooCold: 8,
      tempCold: 16,
      tempHot: 30,
      tempTooHot: 38,
      smoking: smoke,
      smokingKnown: true,
      harshDriving: bump,
      impact: bump,
      overspeed: speed > 50,
      sharpTurn: turn,
      problem: problems.length > 0,
      problems: problems,
      gpsValid: gps,
      latitude: 13.3525,
      longitude: 74.7932,
      speedKmph: speed,
      satellites: gps ? 9 : 0,
      arrived: arrived,
      arrivedAt: 0,
      sentBack: false,
      sentAt: 0,
      homeSafe: false,
      homeAt: 0,
      alerts: alerts
    };
  }

  const realFetch = window.fetch.bind(window);
  window.fetch = function (resource, init) {
    const url = typeof resource === 'string' ? resource : (resource && resource.url) || '';
    if (url === '/data') {
      return Promise.resolve({ ok: true, json: function () { return Promise.resolve(buildData()); } });
    }
    if (url === '/parent/reset') {
      $('arrivedIn').checked = false;
      return Promise.resolve({ ok: true, json: function () { return Promise.resolve({ ok: true }); } });
    }
    if (url === '/parent/home') {
      return Promise.resolve({ ok: true, json: function () { return Promise.resolve({ ok: true }); } });
    }
    return realFetch(resource, init);
  };

  ['tempIn', 'humIn', 'speedIn'].forEach(function (id) {
    $(id).addEventListener('input', syncOutputs);
  });

  document.querySelectorAll('[data-preset]').forEach(function (btn) {
    btn.addEventListener('click', function () { applyPreset(btn.dataset.preset); });
  });

  syncOutputs();
})();
