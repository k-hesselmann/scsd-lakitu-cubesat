import { CircleMarker, MapContainer, Polyline, Popup, TileLayer, useMap } from "react-leaflet"
import type { LatLngBoundsExpression, LatLngExpression } from "leaflet"
import { useEffect, useMemo, useState } from "react"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table"
import { fmt } from "@/lib/format"
import { rawFlagIsValid } from "@/lib/v4Telemetry"
import type { TelemetryRow } from "@/types/telemetry"

function validCoordinate(lat?: number, lon?: number) { return typeof lat === "number" && typeof lon === "number" && Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180 }
function packetTime(row: TelemetryRow) { return row.pc_receive_time_iso ? new Date(row.pc_receive_time_iso).toLocaleTimeString() : "—" }
function MapController({ center, bounds, followLatest, fitRequest }: { center: LatLngExpression; bounds: LatLngBoundsExpression | null; followLatest: boolean; fitRequest: number }) {
  const map = useMap()
  useEffect(() => { if (followLatest) map.setView(center, map.getZoom(), { animate: true }) }, [center, followLatest, map])
  useEffect(() => { if (bounds) map.fitBounds(bounds, { padding: [40, 40] }) }, [bounds, fitRequest, map])
  return null
}

export function PositionPanel({ latest, history }: { latest: TelemetryRow | null; history: TelemetryRow[] }) {
  const [followLatest, setFollowLatest] = useState(true)
  const [fitRequest, setFitRequest] = useState(0)
  const validRows = history.filter((row) => rawFlagIsValid(row.gps_valid_raw) && validCoordinate(row.latitude_deg, row.longitude_deg))
  const trajectory = validRows.map((row) => [row.latitude_deg as number, row.longitude_deg as number] as [number, number])
  const launchPoint = trajectory[0] ?? null
  const latestPoint = latest && rawFlagIsValid(latest.gps_valid_raw) && validCoordinate(latest.latitude_deg, latest.longitude_deg) ? [latest.latitude_deg as number, latest.longitude_deg as number] as [number, number] : null
  const maxAltitudeRow = useMemo(() => validRows.reduce<TelemetryRow | null>((best, row) => !best || (row.gnss_altitude_m ?? -Infinity) > (best.gnss_altitude_m ?? -Infinity) ? row : best, null), [validRows])
  const maxAltitudePoint = maxAltitudeRow && validCoordinate(maxAltitudeRow.latitude_deg, maxAltitudeRow.longitude_deg) ? [maxAltitudeRow.latitude_deg as number, maxAltitudeRow.longitude_deg as number] as [number, number] : null
  const center: LatLngExpression = latestPoint ?? launchPoint ?? [48.1351, 11.582]
  const bounds: LatLngBoundsExpression | null = trajectory.length >= 2 ? trajectory as LatLngBoundsExpression : null

  return <div className="grid h-full min-h-0 gap-3 xl:grid-cols-[460px_minmax(0,1fr)]">
    <div className="grid min-h-0 grid-rows-[minmax(0,1fr)_145px] gap-3">
      <Card className="min-h-0 overflow-hidden"><CardHeader className="px-3 py-1.5"><CardTitle className="text-xs">Valid GPS Position History</CardTitle></CardHeader><CardContent className="h-[calc(100%-32px)] px-3 pb-3"><div className="h-full overflow-auto rounded-md border"><Table className="min-w-max text-xs"><TableHeader className="sticky top-0 bg-background"><TableRow><TableHead>Ground time</TableHead><TableHead>Seq</TableHead><TableHead>Latitude</TableHead><TableHead>Longitude</TableHead><TableHead>GPS alt</TableHead><TableHead>Baro alt</TableHead><TableHead>Ground speed</TableHead><TableHead>Vertical speed</TableHead></TableRow></TableHeader><TableBody>{[...validRows].reverse().map((row, index) => <TableRow key={`${row.sequence_number}-${index}`}><TableCell>{packetTime(row)}</TableCell><TableCell>{fmt(row.sequence_number)}</TableCell><TableCell>{fmt(row.latitude_deg, "", 7)}</TableCell><TableCell>{fmt(row.longitude_deg, "", 7)}</TableCell><TableCell>{fmt(row.gnss_altitude_m, " m", 1)}</TableCell><TableCell>{fmt(row.baro_altitude_m, " m", 1)}</TableCell><TableCell>{fmt(row.ground_speed_ms, " m/s", 2)}</TableCell><TableCell>{fmt(row.vertical_speed_ms, " m/s", 2)}</TableCell></TableRow>)}</TableBody></Table></div></CardContent></Card>
      <Card><CardHeader className="px-3 py-1.5"><CardTitle className="text-xs">Raw-v7 GPS Metrics</CardTitle></CardHeader><CardContent className="grid h-[calc(100%-32px)] grid-cols-2 gap-x-3 gap-y-1 overflow-auto px-3 pb-2 text-[11px]"><div>GPS valid</div><div>{latest ? rawFlagIsValid(latest.gps_valid_raw) ? "YES" : "NO" : "—"}</div><div>GPS altitude</div><div>{fmt(latest?.gnss_altitude_m, " m", 1)}</div><div>Vertical speed</div><div>{fmt(latest?.vertical_speed_ms, " m/s", 2)}</div><div>Ground speed</div><div>{fmt(latest?.ground_speed_ms, " m/s", 2)}</div><div>Trajectory points</div><div>{trajectory.length}</div></CardContent></Card>
    </div>
    <Card className="min-h-0 overflow-hidden"><CardHeader className="flex flex-row items-center justify-between px-3 py-1.5"><CardTitle className="text-xs">GPS Trajectory</CardTitle><div className="flex gap-2"><Button variant="outline" size="sm" className="h-7 px-2 text-xs" onClick={() => setFitRequest((value) => value + 1)}>Auto-fit</Button><Button variant={followLatest ? "default" : "outline"} size="sm" className="h-7 px-2 text-xs" onClick={() => setFollowLatest((value) => !value)}>{followLatest ? "Following" : "Follow"}</Button></div></CardHeader><CardContent className="h-[calc(100%-32px)] px-3 pb-3"><div className="map-dashboard h-full overflow-hidden rounded-lg border bg-muted"><MapContainer center={center} zoom={15} scrollWheelZoom className="h-full w-full"><MapController center={center} bounds={bounds} followLatest={followLatest} fitRequest={fitRequest} /><TileLayer attribution="&copy; OpenStreetMap contributors &copy; CARTO" url="https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png" />{trajectory.length >= 2 ? <Polyline positions={trajectory} pathOptions={{ color: "#2563eb", weight: 4 }} /> : null}{launchPoint ? <CircleMarker center={launchPoint} radius={7} pathOptions={{ color: "#16a34a", fillColor: "#16a34a", fillOpacity: 0.8 }}><Popup>First valid GPS point</Popup></CircleMarker> : null}{maxAltitudePoint ? <CircleMarker center={maxAltitudePoint} radius={7} pathOptions={{ color: "#7c3aed", fillColor: "#7c3aed", fillOpacity: 0.8 }}><Popup>Maximum GPS altitude<br />{fmt(maxAltitudeRow?.gnss_altitude_m, " m", 1)}</Popup></CircleMarker> : null}{latestPoint ? <CircleMarker center={latestPoint} radius={8} pathOptions={{ color: "#dc2626", fillColor: "#dc2626", fillOpacity: 0.9 }}><Popup>Latest valid GPS point<br />Seq {fmt(latest?.sequence_number)}<br />{packetTime(latest as TelemetryRow)}</Popup></CircleMarker> : null}</MapContainer></div></CardContent></Card>
  </div>
}
