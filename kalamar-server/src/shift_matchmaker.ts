import { EventEmitter } from "events";
import { randomUUID } from "crypto";
import type {
  ShiftJoinRequest, ShiftSession, ShiftMode,
  SalmonRunStage, WeaponLoadout,
} from "./shift_types";

const SHIFT_PARTY_SIZE = 4;
const FILL_TIMEOUT_MS = 30_000;

const STAGE_WEAPON_POOL: Record<SalmonRunStage, number[]> = {
  spawning_grounds:        [0, 10, 20, 30, 40, 1000, 1010, 2000, 3000, 4000],
  sockeye_station:         [5, 15, 25, 35, 45, 1001, 1011, 2001, 3001, 4001],
  marooner_bay:            [1, 11, 21, 31, 41, 1002, 1012, 2002, 3002, 4002],
  gone_fission_hydroplant: [2, 12, 22, 32, 42, 1003, 1013, 2003, 3003, 4003],
  jammin_salmon_junction:  [3, 13, 23, 33, 43, 1004, 1014, 2004, 3004, 4004],
};

function resolveQuotas(hazard: number): [number, number, number] {
  if (hazard <= 60)  return [3, 5, 7];
  if (hazard <= 120) return [5, 8, 10];
  if (hazard <= 200) return [7, 10, 13];
  if (hazard <= 280) return [9, 12, 15];
  return [11, 14, 17];
}

interface QueuedPlayer {
  player_id: string;
  connection_id: string;
  rank: string;
  hazard_preference: number;
  mode: ShiftMode;
  joined_at: number;
}

export class ShiftMatchmaker extends EventEmitter {
  private queues: Map<ShiftMode, QueuedPlayer[]> = new Map([
    ["normal", []], ["big_run", []], ["eggstra_work", []],
  ]);
  private fillTimers: Map<ShiftMode, ReturnType<typeof setTimeout>> = new Map();
  private currentStage: SalmonRunStage = "spawning_grounds";

  enqueue(req: ShiftJoinRequest, connection_id: string): void {
    const queue = this.queues.get(req.mode)!;
    if (queue.find(p => p.player_id === req.player_id)) return;
    queue.push({
      player_id: req.player_id, connection_id, rank: req.rank,
      hazard_preference: req.hazard_preference, mode: req.mode,
      joined_at: Date.now(),
    });
    console.log(`[ShiftMM] ${req.mode} queue: ${queue.length}/${SHIFT_PARTY_SIZE}`);
    if (queue.length >= SHIFT_PARTY_SIZE) {
      this.fillParty(req.mode);
    } else if (!this.fillTimers.has(req.mode)) {
      this.fillTimers.set(req.mode, setTimeout(() => this.fillParty(req.mode), FILL_TIMEOUT_MS));
    }
  }

  dequeue(player_id: string): void {
    for (const [, queue] of this.queues) {
      const idx = queue.findIndex(p => p.player_id === player_id);
      if (idx !== -1) queue.splice(idx, 1);
    }
  }

  private fillParty(mode: ShiftMode): void {
    const timer = this.fillTimers.get(mode);
    if (timer) { clearTimeout(timer); this.fillTimers.delete(mode); }
    const queue = this.queues.get(mode)!;
    if (queue.length === 0) return;
    const players = queue.splice(0, SHIFT_PARTY_SIZE);
    const session_id = randomUUID();
    const avgHazard = Math.floor(
      players.reduce((s, p) => s + p.hazard_preference, 0) / players.length
    );
    const hazard_level = Math.min(333, avgHazard);
    const wave_quotas = resolveQuotas(hazard_level);
    const pool = STAGE_WEAPON_POOL[this.currentStage];
    const weapons: WeaponLoadout[] = players.map((p, i) => ({
      player_id: p.player_id,
      weapon_id: pool[(i + parseInt(session_id.slice(0, 4), 16)) % pool.length],
      is_random: false,
    }));
    const session: ShiftSession = {
      type: "shift_session_created",
      session_id, mode, stage: this.currentStage,
      hazard_level, player_ids: players.map(p => p.player_id),
      host_player_id: players[0].player_id, weapons, wave_quotas,
      shift_start_at: new Date(Date.now() + 5000).toISOString(),
    };
    console.log(`[ShiftMM] Session ${session_id} created, ${players.length}p, hazard ${hazard_level}%`);
    this.emit("session_created", session, players);
  }

  rotateStage(stage: SalmonRunStage): void {
    this.currentStage = stage;
    console.log(`[ShiftMM] Stage rotated to ${stage}`);
  }
}
