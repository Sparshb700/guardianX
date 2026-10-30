const $ = (id) => document.getElementById(id);

async function loadStatus() {
  try {
    const res = await fetch('/teacher/data', { cache: 'no-store' });
    if (!res.ok) throw 0;
    const d = await res.json();
    $('link').textContent = 'Connected';
    $('link').className = 'status ok';

    const arrive = $('arrive');
    const sent = $('sent');

    if (d.homeSafe) {
      $('status').textContent = 'Parent confirmed the child reached home safely';
      $('card').className = 'card teacher-card safe';
    } else if (d.sentBack) {
      $('status').textContent = 'Child has been sent back from school';
      $('card').className = 'card teacher-card safe';
    } else if (d.arrived) {
      $('status').textContent = 'Child has reached school safely';
      $('card').className = 'card teacher-card safe';
    } else {
      $('status').textContent = 'Waiting for the child to reach school';
      $('card').className = 'card teacher-card';
    }

    arrive.textContent = d.arrived ? 'Arrival confirmed' : 'Child has reached school safely';
    arrive.disabled = d.arrived;

    sent.textContent = d.sentBack ? 'Sent back from school' : 'Child has been sent back from school';
    sent.disabled = !d.arrived || d.sentBack;
  } catch (e) {
    $('link').textContent = 'Connection lost';
    $('link').className = 'status bad';
  }
}

async function markArrived() {
  const btn = $('arrive');
  btn.disabled = true;
  btn.textContent = 'Confirming...';
  try {
    await fetch('/teacher/arrive', { cache: 'no-store' });
  } catch (e) {}
  await loadStatus();
}

async function markSentBack() {
  const btn = $('sent');
  btn.disabled = true;
  btn.textContent = 'Confirming...';
  try {
    await fetch('/teacher/sent', { cache: 'no-store' });
  } catch (e) {}
  await loadStatus();
}

$('arrive').addEventListener('click', markArrived);
$('sent').addEventListener('click', markSentBack);
setInterval(loadStatus, 2000);
loadStatus();
