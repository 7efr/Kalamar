import React, { useState, useEffect, useCallback } from "react";
import { invoke } from "@tauri-apps/api/core";

type ShiftMode = "normal" | "big_run" | "eggstra_work";
type SalmonRunRank =
  | "apprentice" | "part_timer" | "go_getter"
  | "overachiever" | "profreshional"
  | "profreshional_plus_0" | "profreshional_plus_1"
  | "profreshional_plus_2" | "profreshional_plus_3"
  | "eggsecutive_vp";

interface ShiftScheduleEntry {
  mode: ShiftMode;
  stage: string;
  start_time: string;
  end_time: string;
  weapon_pool: string[];
}

interface GrizzcoScreenProps {
  playerId: string;
  serverUrl: string | null;
  onShiftJoined: (sessionId: string) => void;
}

const RANK_DISPLAY: Record<SalmonRunRank, string> = {
  apprentice:           "Apprentice",
  part_timer:           "Part-Timer",
  go_getter:            "Go-Getter",
  overachiever:         "Overachiever",
  profreshional:        "Profreshional",
  profreshional_plus_0: "Profreshional +0",
  profreshional_plus_1: "Profreshional +1",
  profreshional_plus_2: "Profreshional +2",
  profreshional_plus_3: "Profreshional +3",
  eggsecutive_vp:       "Eggsecutive VP",
};

const RANK_HAZARD: Record<SalmonRunRank, number> = {
  apprentice:           20,
  part_timer:           60,
  go_getter:            100,
  overachiever:         140,
  profreshional:        200,
  profreshional_plus_0: 220,
  profreshional_plus_1: 253,
  profreshional_plus_2: 287,
  profreshional_plus_3: 320,
  eggsecutive_vp:       333,
};

export const GrizzcoScreen: React.FC<GrizzcoScreenProps> = ({
  playerId, serverUrl, onShiftJoined,
}) => {
  const [rank, setRank] = useState<SalmonRunRank>("part_timer");
  const [schedule, setSchedule] = useState<ShiftScheduleEntry[]>([]);
  const [selectedMode, setSelectedMode] = useState<ShiftMode>("normal");
  const [queueState, setQueueState] = useState<"idle"|"queuing"|"matched"|"error">("idle");
  const [queueSize, setQueueSize] = useState<number>(0);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);
  const [sessionId, setSessionId] = useState<string | null>(null);

  useEffect(() => {
    invoke<SalmonRunRank>("get_salmon_run_rank").then(setRank).catch(() => setRank("part_timer"));
  }, []);

  useEffect(() => {
    if (!serverUrl) return;
    const fetchSchedule = async () => {
      try {
        const res = await fetch(`${serverUrl}/api/shifts/schedule`);
        if (res.ok) setSchedule(await res.json());
      } catch { /* offline */ }
    };
    fetchSchedule();
    const id = setInterval(fetchSchedule, 60_000);
    return () => clearInterval(id);
  }, [serverUrl]);

  const joinQueue = useCallback(async () => {
    if (!serverUrl) { setErrorMsg("No community server configured."); return; }
    setQueueState("queuing"); setErrorMsg(null);
    try {
      const ws = new WebSocket(`${serverUrl.replace(/^http/, "ws")}/shift/queue`);
      ws.onopen = () => ws.send(JSON.stringify({
        type: "shift_join", player_id: playerId, rank,
        hazard_preference: RANK_HAZARD[rank], mode: selectedMode,
      }));
      ws.onmessage = (event) => {
        const msg = JSON.parse(event.data);
        if (msg.type === "queue_size_update") setQueueSize(msg.size);
        else if (msg.type === "shift_session_created") {
          setSessionId(msg.session_id); setQueueState("matched");
          ws.close(); onShiftJoined(msg.session_id);
        } else if (msg.type === "queue_error") {
          setErrorMsg(msg.reason); setQueueState("error"); ws.close();
        }
      };
      ws.onerror = () => { setErrorMsg("Connection to server lost."); setQueueState("error"); };
      ws.onclose = (e) => {
        if (queueState === "queuing" && !e.wasClean) {
          setErrorMsg("Server closed the connection unexpectedly."); setQueueState("error");
        }
      };
    } catch (err) { setErrorMsg(String(err)); setQueueState("error"); }
  }, [serverUrl, playerId, rank, selectedMode, onShiftJoined, queueState]);

  const cancelQueue = useCallback(() => { setQueueState("idle"); setQueueSize(0); }, []);

  return (
    <div className="grizzco-screen">
      <header className="grizzco-header">
        <h1>Grizzco Industries</h1>
        <div className="rank-badge">
          <span className="rank-label">Rank</span>
          <span className="rank-value">{RANK_DISPLAY[rank]}</span>
          <span className="hazard-value">Hazard {RANK_HAZARD[rank]}%</span>
        </div>
      </header>
      <section className="shift-schedule">
        <h2>Current Rotation</h2>
        {schedule.length === 0 ? (
          <p className="no-schedule">{serverUrl ? "Loading schedule..." : "Connect to a community server to see the shift schedule."}</p>
        ) : (
          <ul className="schedule-list">
            {schedule.map((entry, i) => (
              <li key={i}
                className={`schedule-entry ${entry.mode === selectedMode ? "selected" : ""}`}
                onClick={() => setSelectedMode(entry.mode)}
              >
                <span className="mode-name">
                  {entry.mode === "normal" && "Salmon Run"}
                  {entry.mode === "big_run" && "Big Run"}
                  {entry.mode === "eggstra_work" && "Eggstra Work"}
                </span>
                <span className="stage-name">{entry.stage}</span>
                <ul className="weapon-pool">
                  {entry.weapon_pool.map((w, j) => <li key={j}>{w}</li>)}
                </ul>
              </li>
            ))}
          </ul>
        )}
      </section>
      <section className="queue-section">
        {queueState === "idle" && <button className="join-btn" onClick={joinQueue}>Go to Work</button>}
        {queueState === "queuing" && (
          <div className="queuing-state">
            <p>Looking for coworkers... ({queueSize}/4)</p>
            <button className="cancel-btn" onClick={cancelQueue}>Leave Queue</button>
          </div>
        )}
        {queueState === "matched" && <div className="matched-state"><p>Shift starting! Session: {sessionId}</p></div>}
        {queueState === "error" && (
          <div className="error-state">
            <p className="error-msg">{errorMsg}</p>
            <button className="retry-btn" onClick={() => setQueueState("idle")}>Try Again</button>
          </div>
        )}
      </section>
    </div>
  );
};

export default GrizzcoScreen;
