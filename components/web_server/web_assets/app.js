const statusElement = document.querySelector('#connection-status');
const networksElement = document.querySelector('#saved-networks');
const form = document.querySelector('#wifi-form');
const message = document.querySelector('#message');

async function loadStatus() {
  const response = await fetch('/api/status');
  if (!response.ok) throw new Error('Не удалось получить статус');
  const status = await response.json();
  const radio = status.internet_radio || {};
  statusElement.textContent = `Режим: ${status.mode}; IP: ${status.ip || 'нет'}; сеть: ${status.active_ssid || 'нет'}; радио: ${radio.state || 'нет'}; станция: ${radio.station || 'нет'}`;
  networksElement.replaceChildren(...status.saved_ssids.map((ssid) => {
    const item = document.createElement('li');
    item.textContent = ssid;
    return item;
  }));
}

form.addEventListener('submit', async (event) => {
  event.preventDefault();
  message.textContent = 'Сохранение…';
  const response = await fetch('/api/wifi', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ssid: form.ssid.value, password: form.password.value}),
  });
  if (!response.ok) {
    message.textContent = await response.text();
    return;
  }
  message.textContent = 'Настройка сохранена. Устройство перезапускается…';
});

loadStatus().catch((error) => { statusElement.textContent = error.message; });
