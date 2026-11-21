# reTerminal E1001 Dashboard

The entrance-hall e-ink dashboard: weather and calendar events from Home Assistant, rendered on a 7.5" e-paper display by ESPHome firmware.

## Language

**Calendar Event**:
A single entry fetched from a Home Assistant calendar — a start, an end, and a summary.
_Avoid_: appointment, booking

**Event Span**:
The days an event covers, from its start date to its last covered day. A single-day event's span is one day.
_Avoid_: duration, range

**Continuation Row**:
A row under a day header stating that a spanning event is running that day — it shows only the time edge that day owns.
_Avoid_: repeat, duplicate event

**All-Day Event**:
An event without clock times. Its end date is exclusive (the iCalendar convention): a Fri–Sun all-day event ends "Monday".
_Avoid_: full-day event

**Day Header**:
The weekday-named band that a day's event rows render under, carrying the ISO week number.

**Fetch Window**:
The 14-day lookahead from now within which calendar events are fetched.
