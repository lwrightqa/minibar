#!/usr/bin/env python3
"""
make_fixtures.py: writes the calendar host tests' .ics fixtures. Owner: calendar builder.

The feeds are written the way the services write them, so the tests read what the bar will read:
  google_week.ics  Google Calendar's secret iCal export (PRODID, X-WR-TIMEZONE, Google's VTIMEZONE blocks, attendees,
                   Meet descriptions, alarms, lines folded at 75 octets with CRLF, one fold inside a UTF-8 character)
                   for the week of Monday 2026-10-05 in America/Los_Angeles: a weekday standup with an EXDATE, a moved
                   1:1 whose override comes BEFORE its master, a cancelled instance, a private event, a Free event,
                   an all-day event, a declined event, a meeting across midnight, a London meeting, an instance moved
                   into the window and one moved out, a monthly 1TU, a finished COUNT series, a series split with UNTIL.
  google_dst.ics   recurring meetings across the US and EU daylight-saving changes of 2026 (EU Oct 25, US Nov 1 and
                   Mar 8): local times kept, the ambiguous 1:30 AM, the 2:30 AM gap, COUNT and UTC UNTIL across the
                   change, EXDATE with TZID, INTERVAL=2 with WKST, monthly 2TU / -1FR / 31st, yearly.
  outlook.ics      an Outlook / Exchange published calendar, LF line ends: Windows zone names defined by VTIMEZONE
                   ("Pacific Standard Time", "W. Europe Standard Time", a custom fixed zone), BYSETPOS=-1, a
                   Mozilla-style TZID, an unknown TZID, a floating time, BUSYSTATUS FREE, a cancelled meeting.
  google_signin.html  what a broken secret address can return instead of a calendar (a sign-in page).

Run it from anywhere; it writes next to itself. The expected values live in test_feeds.c.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SELF = 'sam.lee@example.com'


def fold(line, width=75):
    """RFC 5545 folding by octets, the way Google does it (it may split a UTF-8 character)."""
    b = line.encode('utf-8') if isinstance(line, str) else line
    out = [b[:width]]
    b = b[width:]
    while b:
        out.append(b' ' + b[:width - 1])
        b = b[width - 1:]
    return out


def write(name, lines, eol=b'\r\n'):
    data = b''
    for ln in lines:
        if isinstance(ln, tuple):           # pre-folded raw pieces
            data += eol.join(ln) + eol
        else:
            data += eol.join(fold(ln)) + eol
    with open(os.path.join(HERE, name), 'wb') as f:
        f.write(data)
    print(f'wrote {name}: {len(data)} bytes')


GOOGLE_HEAD = [
    'BEGIN:VCALENDAR',
    'PRODID:-//Google Inc//Google Calendar 70.9054//EN',
    'VERSION:2.0',
    'CALSCALE:GREGORIAN',
    'METHOD:PUBLISH',
    f'X-WR-CALNAME:{SELF}',
    'X-WR-TIMEZONE:America/Los_Angeles',
    'BEGIN:VTIMEZONE',
    'TZID:America/Los_Angeles',
    'X-LIC-LOCATION:America/Los_Angeles',
    'BEGIN:DAYLIGHT',
    'TZOFFSETFROM:-0800',
    'TZOFFSETTO:-0700',
    'TZNAME:PDT',
    'DTSTART:19700308T020000',
    'RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU',
    'END:DAYLIGHT',
    'BEGIN:STANDARD',
    'TZOFFSETFROM:-0700',
    'TZOFFSETTO:-0800',
    'TZNAME:PST',
    'DTSTART:19701101T020000',
    'RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU',
    'END:STANDARD',
    'END:VTIMEZONE',
    'BEGIN:VTIMEZONE',
    'TZID:Europe/London',
    'X-LIC-LOCATION:Europe/London',
    'BEGIN:DAYLIGHT',
    'TZOFFSETFROM:+0000',
    'TZOFFSETTO:+0100',
    'TZNAME:BST',
    'DTSTART:19700329T010000',
    'RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU',
    'END:DAYLIGHT',
    'BEGIN:STANDARD',
    'TZOFFSETFROM:+0100',
    'TZOFFSETTO:+0000',
    'TZNAME:GMT',
    'DTSTART:19701025T020000',
    'RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU',
    'END:STANDARD',
    'END:VTIMEZONE',
]

MEET = ('-::~:~::~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~::~:~::-'
        '\\nJoin with Google Meet: https://meet.google.com/kfq-zrvd-dxa\\nOr dial: (US) +1 650-555-0143 PIN: 223 '
        '881 093#\\nMore phone numbers: https://tel.meet/kfq-zrvd-dxa?pin=2238810935611\\n\\nLearn more about Meet '
        'at: https://support.google.com/a/users/answer/9282720\\n\\nPlease do not edit this section.\\n-::~:~::~:~:~:'
        '~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~:~::~:~::-')


def attendee(email, cn, partstat='ACCEPTED', role='REQ-PARTICIPANT'):
    return (f'ATTENDEE;CUTYPE=INDIVIDUAL;ROLE={role};PARTSTAT={partstat};RSVP=TRUE;CN={cn};X-NUM-GUESTS=0:'
            f'mailto:{email}')


def gevent(uid, start, end, summary, tz='America/Los_Angeles', rrule=None, exdates=(), rid=None, location='',
           status='CONFIRMED', transp='OPAQUE', klass=None, me='ACCEPTED', organizer='priya@example.com',
           description='', alarm=True, raw_summary=None, all_day=False):
    """A VEVENT in Google's property order."""
    ln = []
    if all_day:
        ln += [f'DTSTART;VALUE=DATE:{start}', f'DTEND;VALUE=DATE:{end}']
    elif tz == 'UTC':
        ln += [f'DTSTART:{start}Z', f'DTEND:{end}Z']
    else:
        ln += [f'DTSTART;TZID={tz}:{start}', f'DTEND;TZID={tz}:{end}']
    if rrule:
        ln.append(f'RRULE:{rrule}')
    for x in exdates:
        ln.append(f'EXDATE;TZID={tz}:{x}')
    ln += ['DTSTAMP:20261004T150412Z', f'ORGANIZER;CN={organizer}:mailto:{organizer}', f'UID:{uid}']
    if rid:
        ln.append(f'RECURRENCE-ID;TZID={tz}:{rid}')
    ln.append(attendee(organizer, organizer))
    if me:
        ln.append(attendee(SELF, SELF, me))
    ln += ['X-GOOGLE-CONFERENCE:https://meet.google.com/kfq-zrvd-dxa', 'CREATED:20250912T171500Z',
           f'DESCRIPTION:{description}', 'LAST-MODIFIED:20261002T203344Z', f'LOCATION:{location}', 'SEQUENCE:1',
           f'STATUS:{status}']
    out = ['BEGIN:VEVENT'] + ln
    if raw_summary is not None:
        out.append(raw_summary)
    else:
        out.append(f'SUMMARY:{summary}')
    if klass:
        out.append(f'CLASS:{klass}')
    out.append(f'TRANSP:{transp}')
    if alarm:
        # An alarm's DESCRIPTION and ATTENDEE must never count as the event's.
        out += ['BEGIN:VALARM', 'ACTION:EMAIL', 'DESCRIPTION:This is an event reminder', 'SUMMARY:Alarm notification',
                attendee(SELF, SELF, 'DECLINED'), 'TRIGGER:-P0DT0H30M0S', 'END:VALARM']
    out.append('END:VEVENT')
    return out


def google_week():
    L = list(GOOGLE_HEAD)
    LA = 'America/Los_Angeles'
    # 1. Weekday standup since January, skipped on Tuesday 10/6 (EXDATE).
    L += gevent('7kukuqrfedlm2f6ra5o5r2iqel@google.com', '20260105T093000', '20260105T094500', 'Daily standup',
                rrule='FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR', exdates=['20261006T093000'], description=MEET)
    # 2a. The override of the Monday 1:1 comes first in the file (Google doesn't promise an order).
    L += gevent('2p3ccf1u6v9rtlb4v5kq5o0g7d@google.com', '20261005T130000', '20261005T133000',
                '1:1 Priya / Alex (moved)', rid='20261005T110000', location='Priya\'s desk')
    # 2b. Its master: Mondays 11:00 since March 2 (before the March DST change).
    L += gevent('2p3ccf1u6v9rtlb4v5kq5o0g7d@google.com', '20260302T110000', '20260302T113000', '1:1 Priya / Alex',
                rrule='FREQ=WEEKLY;BYDAY=MO', location='Priya\'s desk')
    # 3. Tuesday team sync; the 10/6 instance is cancelled by an override.
    L += gevent('5a8e1c8d0kq1n3h2tbu4fh0g3s@google.com', '20260106T100000', '20260106T110000', 'Team sync',
                rrule='FREQ=WEEKLY;BYDAY=TU', location='Room 2.41 (Lovelace)')
    L += gevent('5a8e1c8d0kq1n3h2tbu4fh0g3s@google.com', '20261006T100000', '20261006T110000', 'Team sync',
                rid='20261006T100000', status='CANCELLED', location='Room 2.41 (Lovelace)')
    # 4. A private event: counts, but its title and location are never kept.
    L += gevent('0vni9j3h8a4uo0p4lq6m4ku2lb@google.com', '20261006T140000', '20261006T150000', 'Design review: secret',
                klass='PRIVATE', location='Board room')
    # 5. Free (transparent): doesn't count.
    L += gevent('3bq1o6c5hn9v2t1m6kp0s5c7f8@google.com', '20261005T150000', '20261005T170000', 'Focus block',
                transp='TRANSPARENT', organizer=SELF)
    # 6. All day: doesn't count.
    L += gevent('6d1t0q8p5n2m4l3k2j1h0g9f8e@google.com', '20261005', '20261006', 'Offsite planning', all_day=True,
                transp='TRANSPARENT')
    # 7. Declined by you: doesn't count.
    L += gevent('1c2b3a4d5e6f7g8h9i0j1k2l3m@google.com', '20261005T160000', '20261005T163000', 'Vendor demo',
                me='DECLINED', organizer='sales@vendor.example')
    # 8. Across midnight: 11:30 PM to 12:30 AM.
    L += gevent('4r5t6y7u8i9o0p1a2s3d4f5g6h@google.com', '20261005T233000', '20261006T003000', 'Release watch',
                organizer=SELF)
    # 9. A London meeting, weekly on Mondays at 17:00 London (9:00 in Los Angeles while both keep summer time).
    L += gevent('9z8y7x6w5v4u3t2s1r0q9p8o7n@google.com', '20260907T170000', '20260907T173000', 'London sync',
                tz='Europe/London', rrule='FREQ=WEEKLY;BYDAY=MO', organizer='ops@example.co.uk')
    # 10. An instance moved INTO the window: Thursday 10/8 10:00 moved to Tuesday 10/6 16:00.
    L += gevent('8h7g6f5e4d3c2b1a0z9y8x7w6v@google.com', '20260903T100000', '20260903T110000', 'Quarterly planning',
                rrule='FREQ=WEEKLY;BYDAY=TH')
    L += gevent('8h7g6f5e4d3c2b1a0z9y8x7w6v@google.com', '20261006T160000', '20261006T170000',
                'Quarterly planning (moved)', rid='20261008T100000')
    # 11. An instance moved OUT of the window: Monday 10/5 14:00 moved to Friday 10/9.
    L += gevent('m1n2b3v4c5x6z7l8k9j0h1g2f3@google.com', '20260907T140000', '20260907T150000', 'Staff meeting',
                rrule='FREQ=WEEKLY;BYDAY=MO')
    L += gevent('m1n2b3v4c5x6z7l8k9j0h1g2f3@google.com', '20261009T140000', '20261009T150000', 'Staff meeting',
                rid='20261005T140000')
    # 12. Folded inside UTF-8 characters: "Café ☕ sync", split after the first byte of é and inside ☕.
    s = 'SUMMARY:Café ☕ sync'.encode('utf-8')
    i = s.index('é'.encode())
    j = s.index('☕'.encode())
    raw = (s[:i + 1], b' ' + s[i + 1:j + 2], b' ' + s[j + 2:])
    ev = gevent('c4f3s7nc0f0ld3dutf8t3st000@google.com', '20261006T113000', '20261006T120000', None, raw_summary='@@')
    k = ev.index('@@')
    L += ev[:k]
    L.append(raw)
    L += ev[k + 1:]
    # 13. Monthly on the first Tuesday.
    L += gevent('a1l2l3h4a5n6d7s8m9o0n1t2h3@google.com', '20260106T090000', '20260106T100000', 'Monthly all-hands',
                rrule='FREQ=MONTHLY;BYDAY=1TU', location='https://meet.google.com/kfq-zrvd-dxa',
                organizer='ceo@example.com')
    # 14. A finished COUNT series (onboarding, 5 weekdays from 9/28).
    L += gevent('o1n2b3o4a5r6d7i8n9g0c1o2u3@google.com', '20260928T080000', '20260928T083000', 'Onboarding',
                rrule='FREQ=DAILY;COUNT=5')
    # 15. A series split with "this and following": the old one ends with UNTIL, the new one starts 10/5.
    L += gevent('s1a2m3o4n5e6o7n8o9n0e1x2y3@google.com', '20260608T100000', '20260608T103000', '1:1 Sam / Alex',
                rrule='FREQ=WEEKLY;UNTIL=20260928T165959Z;BYDAY=MO', organizer=SELF)
    L += gevent('s1a2m3o4n5e6o7n8o9n0e1x2y3_R20261005T170000@google.com', '20261005T100000', '20261005T103000',
                '1:1 Sam / Alex', rrule='FREQ=WEEKLY;BYDAY=MO', organizer=SELF)
    L.append('END:VCALENDAR')
    write('google_week.ics', L)


def google_dst():
    L = list(GOOGLE_HEAD)
    # Weekly planning, Mondays 9:00 Los Angeles since September; skipped on 11/9.
    L += gevent('w1e2e3k4l5y6p7l8a9n0n1i2n3@google.com', '20260907T090000', '20260907T100000', 'Weekly planning',
                rrule='FREQ=WEEKLY;BYDAY=MO', exdates=['20261109T090000'], organizer=SELF)
    # London standup, weekdays 9:00 London since September.
    L += gevent('l1o2n3d4o5n6s7t8a9n0d1u2p3@google.com', '20260901T090000', '20260901T091500', 'London standup',
                tz='Europe/London', rrule='FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR', organizer='ops@example.co.uk')
    # Night ops check, daily 1:30 AM Los Angeles: 1:30 happens twice on 11/1.
    L += gevent('n1i2g3h4t5o6p7s8c9h0e1c2k3@google.com', '20261001T013000', '20261001T014500', 'Night ops check',
                rrule='FREQ=DAILY', organizer=SELF)
    # Early sync, daily 2:30 AM from March 6: 2:30 doesn't exist on 3/8.
    L += gevent('e1a2r3l4y5s6y7n8c9g0a1p2t3@google.com', '20260306T023000', '20260306T030000', 'Early sync',
                rrule='FREQ=DAILY;COUNT=5', organizer=SELF)
    # Six Thursdays at 15:00 from 10/15 (three in summer time, three in winter time).
    L += gevent('c1o2u3n4t5d6s7t8x9y0z1a2b3@google.com', '20261015T150000', '20261015T160000', 'Design crit',
                rrule='FREQ=WEEKLY;COUNT=6;BYDAY=TH')
    # Daily lunch talk 12:00 from 10/28, "ends on Nov 1" (UNTIL is Nov 1 23:59:59 PST in UTC).
    L += gevent('u1n2t3i4l5u6t7c8a9c0r1o2s3@google.com', '20261028T120000', '20261028T123000', 'Lunch talk',
                rrule='FREQ=DAILY;UNTIL=20261102T075959Z')
    # Every other week on Tuesday and Thursday, weeks starting Sunday, from 9/1.
    L += gevent('b1i2w3e4e5k6l7y8t9u0t1h2u3@google.com', '20260901T150000', '20260901T153000', 'Biweekly sync',
                rrule='FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;WKST=SU')
    # Monthly: second Tuesday, last Friday, the 31st.
    L += gevent('m1o2n3t4h5l6y7t8u9e0s1d2a3@google.com', '20260113T110000', '20260113T120000', 'Product council',
                rrule='FREQ=MONTHLY;BYDAY=2TU')
    L += gevent('l1a2s3t4f5r6i7d8a9y0x1y2z3@google.com', '20260130T160000', '20260130T170000', 'Demo day',
                rrule='FREQ=MONTHLY;BYDAY=-1FR')
    L += gevent('t1h2i3r4t5y6f7i8r9s0t1x2y3@google.com', '20260131T100000', '20260131T103000', 'Expense check',
                rrule='FREQ=MONTHLY;BYMONTHDAY=31', organizer=SELF)
    # Yearly, since 2020.
    L += gevent('a1n2n3i4v5e6r7s8a9r0y1x2y3@google.com', '20201027T160000', '20201027T170000', 'Team anniversary',
                rrule='FREQ=YEARLY')
    L.append('END:VCALENDAR')
    write('google_dst.ics', L)


def outlook():
    L = [
        'BEGIN:VCALENDAR',
        'METHOD:PUBLISH',
        'PRODID:Microsoft Exchange Server 2010',
        'VERSION:2.0',
        'X-WR-CALNAME:Calendar',
        'BEGIN:VTIMEZONE',
        'TZID:Pacific Standard Time',
        'BEGIN:STANDARD',
        'DTSTART:16010101T020000',
        'TZOFFSETFROM:-0700',
        'TZOFFSETTO:-0800',
        'RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=1SU;BYMONTH=11',
        'END:STANDARD',
        'BEGIN:DAYLIGHT',
        'DTSTART:16010101T020000',
        'TZOFFSETFROM:-0800',
        'TZOFFSETTO:-0700',
        'RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=2SU;BYMONTH=3',
        'END:DAYLIGHT',
        'END:VTIMEZONE',
        'BEGIN:VTIMEZONE',
        'TZID:W. Europe Standard Time',
        'BEGIN:STANDARD',
        'DTSTART:16010101T030000',
        'TZOFFSETFROM:+0200',
        'TZOFFSETTO:+0100',
        'RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=-1SU;BYMONTH=10',
        'END:STANDARD',
        'BEGIN:DAYLIGHT',
        'DTSTART:16010101T020000',
        'TZOFFSETFROM:+0100',
        'TZOFFSETTO:+0200',
        'RRULE:FREQ=YEARLY;INTERVAL=1;BYDAY=-1SU;BYMONTH=3',
        'END:DAYLIGHT',
        'END:VTIMEZONE',
        # A custom zone that dropped daylight time: the old DAYLIGHT rule ended in 2019.
        'BEGIN:VTIMEZONE',
        'TZID:Customized Time Zone',
        'BEGIN:STANDARD',
        'DTSTART:16010101T000000',
        'TZOFFSETFROM:+0630',
        'TZOFFSETTO:+0530',
        'RRULE:FREQ=YEARLY;UNTIL=20190101T000000Z;BYDAY=1SU;BYMONTH=10',
        'END:STANDARD',
        'BEGIN:DAYLIGHT',
        'DTSTART:16010101T000000',
        'TZOFFSETFROM:+0530',
        'TZOFFSETTO:+0630',
        'RRULE:FREQ=YEARLY;UNTIL=20190101T000000Z;BYDAY=1SU;BYMONTH=4',
        'END:DAYLIGHT',
        'BEGIN:STANDARD',
        'DTSTART:20190101T000000',
        'TZOFFSETFROM:+0530',
        'TZOFFSETTO:+0530',
        'END:STANDARD',
        'END:VTIMEZONE',
    ]

    def ev(uid, start, end, summary, tz='Pacific Standard Time', rrule=None, busy='BUSY', transp='OPAQUE',
           status='CONFIRMED', location='', floating=False):
        out = ['BEGIN:VEVENT', 'DESCRIPTION:\\n']
        if rrule:
            out.append(f'RRULE:{rrule}')
        out += [f'UID:{uid}', f'SUMMARY:{summary}']
        if floating:
            out += [f'DTSTART:{start}', f'DTEND:{end}']
        else:
            out += [f'DTSTART;TZID="{tz}":{start}' if ',' in tz else f'DTSTART;TZID={tz}:{start}',
                    f'DTEND;TZID={tz}:{end}']
        out += ['CLASS:PUBLIC', 'PRIORITY:5', 'DTSTAMP:20261004T150000Z', f'TRANSP:{transp}', f'STATUS:{status}',
                'SEQUENCE:0', f'LOCATION:{location}', 'X-MICROSOFT-CDO-APPT-SEQUENCE:0',
                f'X-MICROSOFT-CDO-BUSYSTATUS:{busy}', 'X-MICROSOFT-CDO-INTENDEDSTATUS:BUSY',
                'X-MICROSOFT-CDO-ALLDAYEVENT:FALSE', 'X-MICROSOFT-CDO-IMPORTANCE:1', 'X-MICROSOFT-CDO-INSTTYPE:0',
                'X-MICROSOFT-DONOTFORWARDMEETING:FALSE', 'X-MICROSOFT-DISALLOW-COUNTER:FALSE', 'END:VEVENT']
        return out

    eid = '040000008200E00074C5B7101A82E00800000000'
    L += ev(eid + 'A1B2C3D4', '20260903T100000', '20260903T110000', 'Architecture review',
            rrule='FREQ=WEEKLY;UNTIL=20261231T180000Z;INTERVAL=2;BYDAY=TH;WKST=SU', location='Conference Room 3')
    L += ev(eid + 'B2C3D4E5', '20260130T160000', '20260130T170000', 'Month-end close',
            rrule='FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1')
    L += ev(eid + 'C3D4E5F6', '20261029T170000', '20261029T173000', 'Team call (Berlin)', tz='W. Europe Standard Time')
    L += ev(eid + 'D4E5F6A7', '20261030T090000', '20261030T093000', 'Bangalore handoff', tz='Customized Time Zone')
    L += ev(eid + 'E5F6A7B8', '20261029T140000', '20261029T150000', 'New York check-in',
            tz='/mozilla.org/20050126_1/America/New_York')
    L += ev(eid + 'F6A7B8C9', '20261029T150000', '20261029T153000', 'Mystery zone', tz='Mars/Olympus_Mons')
    L += ev(eid + 'A7B8C9D0', '20261029T120000', '20261029T123000', 'Floating lunch', floating=True)
    L += ev(eid + 'B8C9D0E1', '20261029T080000', '20261029T090000', 'Tentative hold', busy='FREE',
            transp='TRANSPARENT')
    L += ev(eid + 'C9D0E1F2', '20261029T130000', '20261029T133000', 'Canceled: Budget sync', status='CANCELLED')
    L.append('END:VCALENDAR')
    write('outlook.ics', L, eol=b'\n')


def signin():
    html = ('<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Google Calendar - Sign in to Access '
            '&amp; Edit Your Schedule</title><meta name="viewport" content="width=device-width, initial-scale=1">'
            '<style>' + 'body{font-family:Roboto,Arial,sans-serif;margin:0}' * 80 + '</style></head><body>'
            '<div id="initialView" role="main">' + '<div class="filler"></div>' * 120 +
            '<p>BEGIN:VCALENDAR appears only after the first 4 KB, inside a page</p></div></body></html>\n')
    with open(os.path.join(HERE, 'google_signin.html'), 'w') as f:
        f.write(html)
    print(f'wrote google_signin.html: {len(html)} bytes')


if __name__ == '__main__':
    google_week()
    google_dst()
    outlook()
    signin()
