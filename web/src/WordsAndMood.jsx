import React from 'react';
import {MessageCircle,Volume2,VolumeX,Clock3,Moon,Heart,BookOpen,Info} from 'lucide-react';

const modeLabels={idle:'Idle',focus:'Focus',break:'Break'};
const modeHelp={
  idle:'Pesan saat MindPal tidak sedang menjalankan Pomodoro.',
  focus:'Hanya muncul selama sesi fokus Pomodoro.',
  break:'Hanya muncul selama break Pomodoro.'
};

export default function WordsAndMood({cfg,patch,editMood,setEditMood,editMode,setEditMode,Switch,moodNames,moodEmoji,moodColors}){
  const mode=cfg.speech[editMode];
  const isIdle=editMode==='idle';
  const usingMood=isIdle && (mode.source||'mood')==='mood';
  const selectedMood=moodNames[cfg.mood]||'Neutral';
  const activeList=isIdle ? ((mode.source||'mood')==='mood' ? cfg.moods[selectedMood]?.messages : mode.messages) : mode.messages;
  const count=(activeList||[]).map(t=>String(t).trim()).filter(Boolean).length;
  const changeLines=(path,event)=>patch(path,event.target.value.split('\n').slice(0,12));
  return <>
    <section className="panel">
      <div className="panel-title"><div><h3>Today's mood</h3><p>Ekspresi dan warna MindPal mengikuti mood yang kamu pilih. Hanya memengaruhi pesan Idle jika sumbernya Mood.</p></div></div>
      <div className="mood-grid">{moodNames.map((m,i)=><button type="button" key={m} onClick={()=>patch(['mood'],i)} className={'mood '+(cfg.mood===i?'selected':'')} style={{'--mood':cfg.moods[m]?.color||moodColors[i]}}><span>{moodEmoji[i]}</span><b>{m}</b></button>)}</div>
    </section>
    <section className="panel">
      <div className="panel-title"><div><h3>Message Studio</h3><p>Seluruh kalimat otomatis MindPal dikelola di halaman ini. Tidak ada kalimat tersembunyi di firmware.</p></div></div>
      <div className="pill-row" role="tablist" aria-label="Pilih mode pesan">
        {Object.entries(modeLabels).map(([key,name])=><button key={key} type="button" role="tab" aria-selected={editMode===key} className={'pill '+(editMode===key?'chosen':'')} onClick={()=>setEditMode(key)}>{name}</button>)}
      </div>
      <div className="speech-summary"><div><span className="speech-mode-tag">{modeLabels[editMode].toUpperCase()}</span><h3>{isIdle?'Percakapan saat santai':editMode==='focus'?'Motivasi ketika fokus':'Pengingat istirahat'}</h3><p>{modeHelp[editMode]}</p></div><div className="speech-count"><strong>{count}</strong><small>pesan aktif</small></div></div>
      <div className="setting"><div><strong>Show messages on OLED</strong><p>Matikan jika kamu tidak ingin tulisan mengambil alih layar robot.</p></div><Switch value={mode.textEnabled} onChange={v=>patch(['speech',editMode,'textEnabled'],v)}/></div>
      <div className="setting"><div><strong>Vowel voice</strong><p>Suara nada vokal melalui buzzer. Silent Mode tetap menjadi pengaturan utama.</p></div><Switch value={mode.voiceEnabled} onChange={v=>patch(['speech',editMode,'voiceEnabled'],v)}/></div>
      <div className="fields"><div className="field"><label>Minimum interval (seconds)</label><input type="number" min="3" max="1800" value={mode.minSec} onChange={e=>patch(['speech',editMode,'minSec'],Math.max(3,+e.target.value||3))}/></div><div className="field"><label>Maximum interval (seconds)</label><input type="number" min="3" max="1800" value={mode.maxSec} onChange={e=>patch(['speech',editMode,'maxSec'],Math.max(3,+e.target.value||3))}/></div></div>
      {isIdle&&<>
        <div className="field"><label>Where should Idle messages come from?</label><div className="source-options"><button type="button" className={(usingMood?'chosen ':'')+'source-option'} onClick={()=>patch(['speech','idle','source'],'mood')}><Heart size={18}/><b>Selected mood</b><small>Only use words from today's mood: {selectedMood}</small></button><button type="button" className={(!usingMood?'chosen ':'')+'source-option'} onClick={()=>patch(['speech','idle','source'],'general')}><MessageCircle size={18}/><b>General Idle</b><small>Use one shared Idle list regardless of mood.</small></button></div></div>
        {usingMood&&<div className="notice-slim"><Info size={16}/> Saat mood <b>{selectedMood}</b> dipilih, hanya daftar pesan {selectedMood} yang masuk undian. Tidak digabung dengan pesan Idle General, Focus, atau Break.</div>}
      </>}
      {usingMood?<>
        <div className="field"><label>Edit message library for mood</label><div className="pill-row" style={{marginBottom:0}}>{moodNames.map(m=><button type="button" key={m} onClick={()=>setEditMood(m)} className={'pill '+(editMood===m?'chosen':'')}>{m}{selectedMood===m?' · active':''}</button>)}</div></div>
        <div className="field"><label>{editMood} accent color</label><div className="color-picker"><input type="color" value={cfg.moods[editMood]?.color||'#ffffff'} onChange={e=>patch(['moods',editMood,'color'],e.target.value)}/><span>{cfg.moods[editMood]?.color}</span></div></div>
        <div className="field"><label>Messages for {editMood} (one per line)</label><textarea rows="7" value={(cfg.moods[editMood]?.messages||[]).join('\n')} onChange={e=>changeLines(['moods',editMood,'messages'],e)} placeholder="Tulis satu kalimat per baris..."/></div>
      </>:<div className="field"><label>{isIdle?'General Idle':modeLabels[editMode]} messages (one per line)</label><textarea rows="9" value={(mode.messages||[]).join('\n')} onChange={e=>changeLines(['speech',editMode,'messages'],e)} placeholder="Tulis satu kalimat per baris..."/></div>}
      <p className="hint">Batas 12 kalimat per daftar, maksimal 75 karakter per kalimat. Interval hanya dihitung ketika mode ini aktif; alarm, touch, dan layar khusus dapat menunda pesan.</p>
      <div className="notice-slim"><BookOpen size={16}/><span><b>Daftar kosong = tidak ada ucapan otomatis.</b> Tidak ada fallback ke kalimat dari kode Arduino, termasuk jika hanya suara yang aktif.</span></div>
    </section>
    {isIdle&&<section className="panel"><div className="panel-title"><div><h3>Other Idle libraries</h3><p>All Idle and mood libraries can be edited here without switching pages. The active pool is determined by the source selector above.</p></div></div>
      {usingMood ? <div className="field"><label>General Idle messages (inactive while Mood source is selected)</label><textarea rows="6" value={(cfg.speech.idle.messages||[]).join('\n')} onChange={e=>changeLines(['speech','idle','messages'],e)} placeholder="General Idle messages..."/></div> : <>
        <div className="pill-row">{moodNames.map(m=><button key={m} type="button" onClick={()=>setEditMood(m)} className={'pill '+(editMood===m?'chosen':'')}>{m}</button>)}</div>
        <div className="fields"><div className="field"><label>{editMood} mood messages (inactive while General Idle is selected)</label><textarea rows="6" value={(cfg.moods[editMood]?.messages||[]).join('\n')} onChange={e=>changeLines(['moods',editMood,'messages'],e)} placeholder="Mood messages..."/></div><div className="field"><label>{editMood} accent color</label><div className="color-picker"><input type="color" value={cfg.moods[editMood]?.color||'#ffffff'} onChange={e=>patch(['moods',editMood,'color'],e.target.value)}/><span>{cfg.moods[editMood]?.color}</span></div></div></div>
      </>}
    </section>}
    <div className="note"><Moon size={18}/> Pesan otomatis selalu disembunyikan selama Fun Clicker dan Box Breathing. Simpan perubahan dengan tombol Save changes di pojok kanan atas.</div>
  </>;
}
