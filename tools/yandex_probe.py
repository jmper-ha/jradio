#!/usr/bin/env python3
"""Stage-0 probe for the planned Yandex Music source on jRadio.

Deliberately stdlib-only and deliberately low-level: it issues the same raw
requests and computes the same MD5 signature the firmware will, so a green run
here means the C path is viable - not merely that some Python wrapper works.

What it answers, in order:
  1. Can the device get a token without ever seeing a password? (OAuth device flow)
  2. Is the account's subscription good for full tracks, not 30 s previews?
  3. Do the rotor stations look like a list we can put on a 320x240 screen?
  4. Does the three-request dance really end in a playable MP3 URL?

It also prints the numbers the firmware design depends on: JSON payload sizes
(they must fit a PSRAM buffer), URL lengths (they must fit fixed char arrays),
and per-step latency (that is the between-tracks gap we discussed).
"""

import argparse
import hashlib
import json
import os
import ssl
import stat
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.dom.minidom as minidom
from http.client import HTTPSConnection

# Public OAuth credentials of the official Android app, as used by every
# unofficial client. Not a secret, and not ours.
CLIENT_ID = "23cabbbdc6cd418abb4b39c32c41195d"
CLIENT_SECRET = "53bc75238f0c4d08a118e51fe9203300"
OAUTH_BASE = "https://oauth.yandex.ru"
API_BASE = "https://api.music.yandex.net"
SIGN_SALT = "XGRlBW9FXlekgbPrRHuSiA"
API_HEADERS = {
    "X-Yandex-Music-Client": "YandexMusicAndroid/24023621",
    "User-Agent": "Yandex-Music-API",
}
DEFAULT_STATE_PATH = os.path.expanduser("~/.config/jradio/yandex_token.json")

timings = []


def say(message=""):
    print(message, flush=True)


def step(title):
    say()
    say("=" * 72)
    say(title)
    say("=" * 72)


class HttpError(Exception):
    def __init__(self, status, body, url):
        super().__init__(f"HTTP {status} for {url}: {body[:300]}")
        self.status = status
        self.body = body


def http(method, url, *, params=None, form=None, json_body=None, headers=None,
         token=None, label=None, read_limit=None):
    """One request on a fresh connection - which is what the ESP32 will do
    without keep-alive, so the measured time includes the TLS handshake."""
    if params:
        url = f"{url}?{urllib.parse.urlencode(params)}"
    if json_body is not None:
        data = json.dumps(json_body).encode()
    elif form is not None:
        data = urllib.parse.urlencode(form).encode()
    else:
        data = None
    request = urllib.request.Request(url, data=data, method=method)
    if json_body is not None:
        request.add_header("Content-Type", "application/json")
    for key, value in (headers or {}).items():
        request.add_header(key, value)
    if token:
        request.add_header("Authorization", f"OAuth {token}")

    started = time.monotonic()
    try:
        response = urllib.request.urlopen(request, timeout=30,
                                          context=ssl.create_default_context())
    except urllib.error.HTTPError as error:
        body = error.read()
        elapsed = (time.monotonic() - started) * 1000.0
        if label:
            timings.append((label, elapsed, len(body)))
        raise HttpError(error.code, body.decode("utf-8", "replace"), url) from None

    first_byte = None
    chunks = []
    total = 0
    while True:
        chunk = response.read(16384)
        if not chunk:
            break
        if first_byte is None:
            first_byte = (time.monotonic() - started) * 1000.0
        chunks.append(chunk)
        total += len(chunk)
        if read_limit is not None and total >= read_limit:
            break
    body = b"".join(chunks)
    elapsed = (time.monotonic() - started) * 1000.0
    if label:
        timings.append((label, first_byte if first_byte is not None else elapsed, len(body)))
    return response.status, dict(response.headers), body


def api(method, path, *, token, params=None, form=None, json_body=None, label=None):
    status, _, body = http(method, API_BASE + path, params=params, form=form,
                           json_body=json_body, headers=API_HEADERS, token=token,
                           label=label)
    payload = json.loads(body.decode("utf-8"))
    if isinstance(payload, dict) and payload.get("error"):
        raise HttpError(status, json.dumps(payload["error"]), path)
    result = payload.get("result") if isinstance(payload, dict) else None
    return result if result is not None else payload, len(body)


# --------------------------------------------------------------------------
# Token storage. The token is a password equivalent: it never gets printed and
# never lands inside the repository.
# --------------------------------------------------------------------------

def load_state(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return {}


def save_state(path, state):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    handle = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, stat.S_IRUSR | stat.S_IWUSR)
    with os.fdopen(handle, "w", encoding="utf-8") as stream:
        json.dump(state, stream, indent=2)


def device_flow(state, path):
    """Exactly the flow the firmware will run: ask for a code, show it, poll."""
    device_id = state.get("device_id")
    if not device_id:
        device_id = hashlib.sha1(os.urandom(16)).hexdigest()[:10]

    status, _, body = http("POST", f"{OAUTH_BASE}/device/code", form={
        "client_id": CLIENT_ID,
        "device_id": device_id,
        "device_name": "jRadio",
    }, label="oauth /device/code")
    code = json.loads(body.decode("utf-8"))

    say()
    say("  Откройте на телефоне или компьютере:")
    say(f"      {code['verification_url']}")
    say("  и введите код:")
    say(f"      {code['user_code']}")
    say()
    say(f"  Код действителен {code['expires_in']} с, опрашиваю каждые {code['interval']} с.")
    say("  (Ровно это устройство покажет на своём экране - вводить на нём нечего.)")
    say()

    deadline = time.monotonic() + code["expires_in"]
    while True:
        time.sleep(code["interval"])
        try:
            _, _, body = http("POST", f"{OAUTH_BASE}/token", form={
                "grant_type": "device_code",
                "code": code["device_code"],
                "client_id": CLIENT_ID,
                "client_secret": CLIENT_SECRET,
            })
        except HttpError as error:
            if "authorization_pending" in error.body:
                say("  ... ждём подтверждения")
                if time.monotonic() >= deadline:
                    raise SystemExit("Код истёк, запустите скрипт заново.")
                continue
            raise
        token = json.loads(body.decode("utf-8"))
        state.update({
            "device_id": device_id,
            "access_token": token["access_token"],
            "obtained_at": int(time.time()),
            "expires_in": token.get("expires_in"),
        })
        save_state(path, state)
        say(f"  Токен получен и сохранён в {path} (права 0600).")
        say("  Здесь он намеренно не печатается - это эквивалент пароля.")
        return state["access_token"]


# --------------------------------------------------------------------------
# The probe itself
# --------------------------------------------------------------------------

def pick_variant(variants, prefer_codec):
    """Mirror of the choice the firmware will make: full-quality, decodable,
    highest bitrate. A preview-only answer means the subscription is the
    problem, so it is reported rather than silently downgraded."""
    playable = [v for v in variants if not v.get("preview")]
    previews_only = bool(variants) and not playable
    pool = [v for v in playable if v.get("codec") == prefer_codec] or playable
    best = max(pool, key=lambda v: v.get("bitrateInKbps", 0)) if pool else None
    return best, previews_only


def build_link(xml):
    """The signature the firmware will compute: md5(salt + path-without-slash + s)."""
    document = minidom.parseString(xml)

    def text(tag):
        for element in document.getElementsByTagName(tag):
            for node in element.childNodes:
                if node.nodeType == node.TEXT_NODE:
                    return node.data
        raise SystemExit(f"\u0412 XML \u043d\u0435\u0442 \u0442\u0435\u0433\u0430 <{tag}> - \u0444\u043e\u0440\u043c\u0430\u0442 \u043e\u0442\u0432\u0435\u0442\u0430 \u0438\u0437\u043c\u0435\u043d\u0438\u043b\u0441\u044f.")

    host, path, ts, s = text("host"), text("path"), text("ts"), text("s")
    sign = hashlib.md5((SIGN_SALT + path[1:] + s).encode("utf-8")).hexdigest()
    return f"https://{host}/get-mp3/{sign}/{ts}{path}"


def direct_link(download_info_url):
    _, _, xml = http("GET", download_info_url, label="download-info XML")
    return build_link(xml), len(xml)


def keepalive_gap(token, track_id, codec):
    """Measures the realistic between-tracks gap: everything the firmware would
    do on ONE reused TLS connection, instead of a fresh handshake per request.
    This is the number that decides whether prefetching is even needed."""
    host = "api.music.yandex.net"
    headers = dict(API_HEADERS)
    headers["Authorization"] = f"OAuth {token}"
    connection = HTTPSConnection(host, timeout=30,
                                 context=ssl.create_default_context())
    marks = []

    def timed(label, method, path, request_headers):
        started = time.monotonic()
        connection.request(method, path, headers=request_headers)
        response = connection.getresponse()
        body = response.read() if response.status == 200 else response.read()
        elapsed = (time.monotonic() - started) * 1000.0
        marks.append((label, elapsed))
        return response.status, body

    def path_of(url):
        parts = urllib.parse.urlparse(url)
        return parts.netloc, parts.path + (f"?{parts.query}" if parts.query else "")

    status, body = timed("1. download-info (включая рукопожатие)", "GET",
                         f"/tracks/{track_id}/download-info", headers)
    if status != 200:
        say(f"  Не удалось: HTTP {status}")
        return
    info = json.loads(body).get("result", [])
    best, _ = pick_variant(info, codec)
    if best is None:
        say("  Не удалось выбрать вариант загрузки.")
        return

    xml_host, xml_path = path_of(best["downloadInfoUrl"])
    if xml_host == host:
        status, xml = timed("2. XML подписи (то же соединение)", "GET", xml_path, headers)
    else:
        started = time.monotonic()
        _, _, xml = http("GET", best["downloadInfoUrl"])
        marks.append((f"2. XML подписи (другой хост {xml_host}, своё рукопожатие)",
                      (time.monotonic() - started) * 1000.0))

    link = build_link(xml)
    audio_host, audio_path = path_of(link)
    if audio_host == host:
        started = time.monotonic()
        connection.request("GET", audio_path, headers={"User-Agent": API_HEADERS["User-Agent"]})
        response = connection.getresponse()
        marks.append(("3. аудио, первый байт (то же соединение)",
                      (time.monotonic() - started) * 1000.0))
        response.read(65536)
    else:
        started = time.monotonic()
        status, _, _ = http("GET", link, read_limit=65536)
        marks.append((f"3. аудио, первый байт (другой хост {audio_host}, своё рукопожатие)",
                      (time.monotonic() - started) * 1000.0))
    connection.close()

    width = max(len(name) for name, _ in marks)
    for name, elapsed in marks:
        say(f"    {name:<{width}}  {elapsed:8.0f} мс")
    say()
    say(f"  Итого на стыке при одном соединении: {sum(ms for _, ms in marks):.0f} мс")
    say(f"  Хост XML:   {xml_host}")
    say(f"  Хост аудио: {audio_host}")
    say(f"  Хост API:   {host}")


def main():
    parser = argparse.ArgumentParser(description="Этап 0: проверка Yandex Music для jRadio")
    parser.add_argument("--station", default="user:onyourwave",
                        help="станция ротора, по умолчанию Моя волна")
    parser.add_argument("--codec", default="mp3", choices=["mp3", "aac"],
                        help="предпочитаемый кодек (у нас декодируются оба)")
    parser.add_argument("--seconds", type=int, default=10,
                        help="сколько секунд аудио скачать для проверки")
    parser.add_argument("--save", default=os.path.join(tempfile.gettempdir(),
                                                       "jradio_yandex_probe.mp3"),
                        help="куда положить скачанный фрагмент")
    parser.add_argument("--full-list", action="store_true",
                        help="тянуть полный список станций (2.4 МБ); по умолчанию "
                             "только если станция не нашлась в дашборде")
    parser.add_argument("--stations-shown", type=int, default=15,
                        help="сколько станций распечатать")
    parser.add_argument("--token-file", default=DEFAULT_STATE_PATH)
    parser.add_argument("--relogin", action="store_true",
                        help="забыть сохранённый токен и авторизоваться заново")
    parser.add_argument("--feedback", action="store_true",
                        help="слать ротору radioStarted/trackStarted; по умолчанию нет, "
                             "чтобы проверка не влияла на Мою волну")
    args = parser.parse_args()

    state = load_state(args.token_file)
    if args.relogin:
        # Keep device_id: reusing it means Yandex updates the existing "jRadio"
        # entry in the account's device list instead of adding a new one on
        # every re-login.
        state = {"device_id": state.get("device_id")} if state.get("device_id") else {}
    token = os.environ.get("JRADIO_YANDEX_TOKEN") or state.get("access_token")

    step("1. Авторизация (OAuth Device Flow)")
    if token:
        say(f"  Использую сохранённый токен из {args.token_file}.")
        say("  Нужен новый - запустите с --relogin.")
    else:
        token = device_flow(state, args.token_file)

    step("2. Подписка и аккаунт")
    account, _ = api("GET", "/account/status", token=token, label="/account/status")
    uid = account.get("account", {}).get("uid")
    login = account.get("account", {}).get("login")
    subscription = account.get("subscription", {}) or {}
    plus = account.get("plus", {}) or {}
    say(f"  Аккаунт: {login} (uid {uid})")
    say(f"  Подписка: {json.dumps(subscription, ensure_ascii=False)[:200]}")
    say(f"  Plus: {json.dumps(plus, ensure_ascii=False)[:200]}")

    rotor_status, _ = api("GET", "/rotor/account/status", token=token,
                          label="/rotor/account/status")
    say(f"  Ротор доступен: {bool(rotor_status)}")

    step("3. Станции")
    dashboard, dashboard_bytes = api("GET", "/rotor/stations/dashboard", token=token,
                                     label="/rotor/stations/dashboard")
    personal = dashboard.get("stations", [])
    say(f"  Личный дашборд: {len(personal)} станций, JSON {dashboard_bytes} байт")

    id_for_from = None
    for entry in personal:
        station = entry.get("station", {})
        ident = station.get("id", {})
        tag = f"{ident.get('type')}:{ident.get('tag')}"
        say(f"    {tag:<28} {station.get('name'):<24} idForFrom={station.get('idForFrom')}")
        if tag == args.station:
            id_for_from = station.get("idForFrom")

    # The personal wave lives only in the dashboard, so the 2.4 MB catalogue is
    # worth downloading only when the station asked for is not there.
    if id_for_from and not args.full_list:
        say()
        say("  Полный список (2.4 МБ) не запрашиваю: станция нашлась в дашборде.")
        say("  Нужен целиком - запустите с --full-list.")
    else:
        say()
        stations, stations_bytes = api("GET", "/rotor/stations/list", token=token,
                                       params={"language": "ru"},
                                       label="/rotor/stations/list")
        say(f"  Полный список: {len(stations)} станций, JSON {stations_bytes} байт")
        say(f"  На станцию в среднем: {stations_bytes // max(1, len(stations))} байт")
        longest = 0
        shown = 0
        for entry in stations:
            station = entry.get("station", {})
            ident = station.get("id", {})
            tag = f"{ident.get('type')}:{ident.get('tag')}"
            longest = max(longest, len(station.get("name", "")))
            if tag == args.station and not id_for_from:
                id_for_from = station.get("idForFrom")
            if shown < args.stations_shown:
                say(f"    {tag:<28} {station.get('name')}")
                shown += 1
        if len(stations) > shown:
            say(f"    ... и ещё {len(stations) - shown}")
        say()
        say(f"  Самое длинное название: {longest} символов")

    say(f"  idForFrom для {args.station}: {id_for_from}")

    step(f"4. Цепочка треков станции {args.station}")
    batch, batch_bytes = api("GET", f"/rotor/station/{args.station}/tracks", token=token,
                             params={"settings2": "True"}, label="/rotor/.../tracks")
    sequence = batch.get("sequence", [])
    batch_id = batch.get("batchId")
    say(f"  Треков в порции: {len(sequence)}; JSON целиком: {batch_bytes} байт")
    say(f"  batchId: {batch_id}")
    for item in sequence:
        track = item.get("track") or {}
        artists = ", ".join(a.get("name", "") for a in track.get("artists", []))
        say(f"    {track.get('id'):>12}  {artists} - {track.get('title')}")

    first = (sequence[0].get("track") or {}) if sequence else {}
    track_id = first.get("id")
    if not track_id:
        raise SystemExit("Станция не вернула ни одного трека - дальше проверять нечего.")

    # The contract the firmware implements, measured 2026-08-23 and re-measured
    # 2026-08-24 with every field probed one at a time:
    #   - the body must be JSON. Form-encoded - what the Python library sends -
    #     is refused with HTTP 400 "condition is not met" whatever the fields say.
    #   - `timestamp` is required (400 without it); an int is accepted, and so
    #     is 0, so a device whose clock has not synchronised can still report.
    #   - `trackId` is required for everything except radioStarted.
    #   - `totalPlayedSeconds` is required for trackFinished and skip.
    #   - `from` on radioStarted and the batch-id query are both OPTIONAL.
    #   - an unknown `type` is refused with HTTP 400.
    # Each POST measured 90-240 ms.
    if args.feedback:
        say()
        now = time.time()
        duration = int((first.get("durationMs") or 0) / 1000)
        for name, body in (
            ("radioStarted", {"type": "radioStarted", "timestamp": now,
                              "from": id_for_from or args.station.replace(":", "-")}),
            ("trackStarted", {"type": "trackStarted", "timestamp": now,
                              "trackId": track_id}),
            ("trackFinished", {"type": "trackFinished", "timestamp": now,
                               "trackId": track_id, "totalPlayedSeconds": duration}),
        ):
            try:
                api("POST", f"/rotor/station/{args.station}/feedback", token=token,
                    params={"batch-id": batch_id} if batch_id else None,
                    json_body=body, label=f"feedback {name}")
                say(f"  {name}: принят")
            except HttpError as error:
                say(f"  {name}: ОТКЛОНЁН - {error.body[:200]}")
        # `skip` is deliberately not sent from here: it tells the station the
        # track was rejected, and this probe never played it.
        say("  skip: не шлём - прошивка отправляет его, когда трек правда пропустили.")
    else:
        say()
        say("  Фидбек не отправляется (включается флагом --feedback).")
        say("  Цепочка всё равно сдвигается параметром queue=<id предыдущего трека>,")
        say("  но без фидбека ротор не узнаёт, что треки прослушаны - и следующая")
        say("  сессия начинается там же, где началась эта.")

    step("5. Прямая ссылка на трек (три запроса + MD5, как в прошивке)")
    info, info_bytes = api("GET", f"/tracks/{track_id}/download-info", token=token,
                           label="/tracks/{id}/download-info")
    say(f"  Вариантов загрузки: {len(info)}; JSON: {info_bytes} байт")
    for variant in info:
        say(f"    codec={variant.get('codec'):<4} "
            f"bitrate={variant.get('bitrateInKbps'):>4} "
            f"preview={variant.get('preview')} "
            f"url_len={len(variant.get('downloadInfoUrl', ''))}")

    best, previews_only = pick_variant(info, args.codec)
    if previews_only:
        raise SystemExit(
            "\n  ПРОВАЛ: доступны только preview-варианты. Так отвечает API без\n"
            "  активной подписки - полные треки не отдаются, играть будет 30 секунд.")
    if best is None:
        raise SystemExit("\n  ПРОВАЛ: не нашлось ни одного варианта загрузки.")
    say()
    say(f"  Выбран: {best['codec']} {best['bitrateInKbps']} kbps")

    say(f"  Хост downloadInfoUrl: {urllib.parse.urlparse(best['downloadInfoUrl']).netloc}")
    link, xml_bytes = direct_link(best["downloadInfoUrl"])
    say(f"  XML ответа: {xml_bytes} байт")
    say(f"  Длина итогового URL: {len(link)} символов "
        "(в прошивке это буфер фиксированного размера)")
    say(f"  Хост: {urllib.parse.urlparse(link).netloc}")

    step("6. Проигрываемость")
    approx_bytes = max(1, best["bitrateInKbps"] * 1000 // 8 * args.seconds)
    status, headers, audio = http("GET", link, label="аудио: до первого байта",
                                  read_limit=approx_bytes)
    say(f"  HTTP {status}, Content-Type: {headers.get('Content-Type')}")
    say(f"  Полный размер трека: {headers.get('Content-Length')} байт")
    say(f"  Скачано для проверки: {len(audio)} байт")
    if audio[:3] == b"ID3":
        say("  Начинается с тега ID3 - это MP3.")
    elif audio[:1] == b"\xff":
        say("  Начинается с MPEG sync - это MP3 без тега.")
    else:
        say(f"  ВНИМАНИЕ: неожиданное начало файла: {audio[:8]!r}")
    with open(args.save, "wb") as handle:
        handle.write(audio)
    say(f"  Фрагмент сохранён: {args.save}")
    say(f"  Проверить на слух:  mpv {args.save}")
    say("  (Сама ссылка живёт около минуты, файл - сколько угодно.)")

    step("7. Что это значит для прошивки")
    say("  Тайминги, свежее соединение на каждый запрос - то есть с TLS-рукопожатием,")
    say("  ровно как у ESP32 без keep-alive. На плате будет заметно медленнее:")
    say()
    width = max(len(name) for name, _, _ in timings)
    for name, elapsed, size in timings:
        say(f"    {name:<{width}}  {elapsed:8.0f} мс   {size:>9} байт")
    say()
    per_track = [ms for name, ms, _ in timings
                 if name in ("/tracks/{id}/download-info", "download-info XML",
                             "аудио: до первого байта")]
    say(f"  Сумма трёх запросов на стыке треков: {sum(per_track):.0f} мс на этом PC.")
    say("  Это верхняя оценка: каждый запрос платил за своё рукопожатие.")

    step("8. Тот же стык, но по одному переиспользуемому соединению")
    say("  Если хосты совпадают, прошивка может держать одно TLS-соединение")
    say("  и не платить за рукопожатие на каждом треке.")
    say()
    keepalive_gap(token, track_id, args.codec)


if __name__ == "__main__":
    try:
        main()
    except HttpError as error:
        say(f"\nОШИБКА ЗАПРОСА: {error}")
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
