# Flight-data gap log

Generated from the internal-memory archive on 2026-07-28.

## CSV continuity

No CSV file is missing from the firmware session sequence.  Within each boot,
the available session numbers are consecutive: B1 1--5, B2 1--3, B3 1--4,
B4 1--15, B5 1--207, B6 1--92, B7 1--2, and B8--B10 1--4 each.

There are also no gaps greater than two seconds between consecutive logged row
timestamps after ordering within a boot.  Thus, the archive contains no
evidence of missing CSV files or a multi-second CSV sampling interruption.

Three valid RAW images do not have a corresponding *successful* Coral block in
the CSV.  This is a missing telemetry association, not a missing CSV file:

| Boot | Coral frame | Time reference | Note |
| --- | ---: | --- | --- |
| B2 | 147 | unavailable | No GNSS time anchor in this boot. |
| B6 | 107 | 2026-07-28 11:08:48.979 CEST (sequence-cadence estimate) | RAW exists; the success record is absent. |
| B6 | 1233 | 2026-07-28 18:21:37.257 CEST (sequence-cadence estimate) | RAW exists; the success record is absent. |

## Coral sequence jumps

The following are 16 jumps in the stored Coral sequence numbers, representing
900 skipped sequence values in total.  A sequence jump does not prove that a
file was corrupted: the firmware writes a RAW only after a CRC-valid reception
and deletes a failed/partial frame.  It does show that those sequence values
are absent from this archive.

### Time-ordered jumps with GNSS-correlated CEST times

| Boot | Last stored → next stored | Missing sequence values | Time window (CEST) |
| --- | --- | ---: | --- |
| B5 | 244 → 246 | 1 | 13:34:21.998 → 13:34:56.490 |
| B5 | 295 → 297 | 1 | 13:49:19.387 → 13:49:54.689 |
| B5 | 369 → 371 | 1 | 14:10:54.536 → 14:11:28.342 |
| B5 | 408 → 410 | 1 | 14:22:14.745 → 14:22:49.303 |
| B5 | 415 → 417 | 1 | 14:24:15.431 → 14:24:49.905 |
| B5 | 454 → 456 | 1 | 14:35:38.000 → 14:36:12.312 |
| B5 | 620 → 622 | 1 | 15:23:24.350 → 15:23:58.736 |
| B5 | 649 → 651 | 1 | 15:31:55.341 → 15:32:29.418 |
| B5 | 729 → 731 | 1 | 15:54:55.840 → 15:55:30.000 |
| B5 | 816 → 818 | 1 | 16:20:09.465 → 16:20:43.842 |

### Sequence jumps that are not time-orderable from the archive

For these records, the surrounding OBC tick times run backwards or reset. They
are therefore separate/restarted capture blocks, not defensible elapsed-time
gaps.  Their GNSS date/time cannot be recovered from the archive.

| Boot | Stored sequence jump | Missing sequence values | Tick evidence |
| --- | --- | ---: | --- |
| B1 | 142 → 148 | 5 | 99.611 s → 15.955 s |
| B3 | 161 → 163 | 1 | 17.233 s → 52.373 s |
| B3 | 165 → 174 | 8 | 87.599 s → 31.723 s |
| B4 | 166 → 177 | 10 | 32.309 s → 32.904 s |
| B5 | 168 → 217 | 48 | 50.671 s → 32.433 s |
| B6 | 107 → 926 | 818 | frame 107 is estimated at 11:08:48.979 CEST; the next stored record is 16:52:00.788 CEST in a separate CSV recording |

## Timestamp qualification

The firmware stores only GNSS `HHMMSS`, not the GNSS calendar date. The date
in the CEST entries above is the archive-date assumption (2026-07-28), while
the clock times are correlated from GNSS time-of-day. See
`flight_data_audit.json` for the complete per-frame mapping and provenance.
