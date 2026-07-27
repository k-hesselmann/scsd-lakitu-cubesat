import { useEffect, useState } from "react"
import {
  ALERT_THRESHOLDS_UPDATED_EVENT,
  loadAlertThresholds,
  saveAlertThresholds,
  type AlertThresholds,
} from "@/lib/thresholds"

export function useAlertThresholds() {
  const [thresholds, setThresholdsState] = useState<AlertThresholds>(() =>
    loadAlertThresholds(),
  )

  useEffect(() => {
    function reload(event: Event) {
      if (event instanceof CustomEvent && event.detail) {
        setThresholdsState(event.detail as AlertThresholds)
        return
      }

      setThresholdsState(loadAlertThresholds())
    }

    window.addEventListener("storage", reload)
    window.addEventListener(ALERT_THRESHOLDS_UPDATED_EVENT, reload)

    return () => {
      window.removeEventListener("storage", reload)
      window.removeEventListener(ALERT_THRESHOLDS_UPDATED_EVENT, reload)
    }
  }, [])

  function setThresholds(next: AlertThresholds) {
    setThresholdsState(next)
    saveAlertThresholds(next)
  }

  return { thresholds, setThresholds }
}
