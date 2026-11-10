import React,{useState} from 'react';
import {AlarmClock,CalendarDays,Clock3,Music2,Plus,Trash2,Pencil,Info,Check,Volume2} from 'lucide-react';
import './alarms.css';

export const TONES=[
  {name:'Dun dun ding',desc:'Tiga nada singkat'},
  {name:'Morning Bloom',desc:'Nada naik yang lembut'},
  {name:'Soft Rise',desc:'Alarm perlahan yang menenangkan'},
  {name:'Bright Sparkle',desc:'Nada cerah dan ceria'},
  {name:'Cozy Waltz',desc:'Melodi ringan bernuansa hangat'},
  {name:'Little Victory',desc:'Perayaan kecil yang semangat'},
];
const DAYS=['Min','Sen','Sel','Rab','Kam','Jum','Sab'];
const WD_ORDER=[1,2,3,4,5,6,0];
const MAX_ALARMS=12;
const jakartaParts=(dt=new Date())=>{
  const parts=new Intl.DateTimeFormat('en-GB',{timeZone:'Asia/Jakarta',year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).formatToParts(dt);
  return Object.fromEntries(parts.filter(x=>x.type!=='literal').map(x=>[x.type,x.value]));
};
const jakartaDate=()=>{const d=jakartaParts();return `${d.year}-${d.month}-${d.day}`;};
const defaultAlarm=()=>({id:'',label:'Alarm baru',time:'07:00',repeat:'weekly',weekdays:[0,1,2,3,4,5,6],date:jakartaDate(),message:'Good morning! Time to shine!',tone:1,enabled:true,snoozeMin:5});
function describeRepeat(a){if(a.repeat==='once')return `Sekali · ${a.date}`;const d=[...(a.weekdays||[])].sort();if(d.length===7)return 'Setiap hari';if(JSON.stringify(d)===JSON.stringify([1,2,3,4,5]))return 'Sen–Jum';if(JSON.stringify(d)===JSON.stringify([0,6]))return 'Sab–Min';return WD_ORDER.filter(n=>d.includes(n)).map(n=>DAYS[n]).join(', ')||'Tidak ada hari';}
function nextFire(a){if(!a.enabled)return 'Nonaktif';const now=jakartaParts();const time=(a.time||'00:00').split(':').map(Number);const current=Date.UTC(+now.year,+now.month-1,+now.day,+now.hour,+now.minute);
 for(let i=0;i<15;i++){
  const day=new Date(Date.UTC(+now.year,+now.month-1,+now.day+i));const y=day.getUTCFullYear(),m=String(day.getUTCMonth()+1).padStart(2,'0'),d=String(day.getUTCDate()).padStart(2,'0');const date=`${y}-${m}-${d}`;
  if(a.repeat==='once' ? a.date!==date : !(a.weekdays||[]).includes(day.getUTCDay()))continue;
  const next=Date.UTC(y,+m-1,+d,time[0],time[1]);if(next>current){return i===0?'Hari ini':i===1?'Besok':`${DAYS[day.getUTCDay()]}, ${d}/${m}`;}
 }
 return a.repeat==='once'?'Sudah berlalu':'Menunggu jadwal';
}
export function previewTone(choice=0){
  const AC=window.AudioContext||window.webkitAudioContext;if(!AC)return;
  const sequences=[
    [[392,0.18],[392,0.18],[784,0.42]],
    [[523,.2],[659,.2],[784,.24],[988,.36]],
    [[440,.24],[523,.24],[659,.26],[784,.4]],
    [[784,.16],[988,.16],[1175,.22],[1568,.4]],
    [[523,.25],[659,.25],[784,.32],[659,.23],[523,.48]],
    [[523,.17],[784,.18],[988,.22],[784,.18],[1175,.48]]
  ];
  const ctx=new AC();let start=ctx.currentTime+.06;
  (sequences[Number(choice)]||sequences[0]).forEach(([hz,len])=>{const osc=ctx.createOscillator(),gain=ctx.createGain();osc.type='sine';osc.frequency.setValueAtTime(hz,start);gain.gain.setValueAtTime(.0001,start);gain.gain.exponentialRampToValueAtTime(.08,start+.02);gain.gain.exponentialRampToValueAtTime(.0001,start+len);osc.connect(gain);gain.connect(ctx.destination);osc.start(start);osc.stop(start+len+.01);start+=len+.06;});
  setTimeout(()=>ctx.close().catch(()=>{}),Math.ceil((start-ctx.currentTime+1)*1000));
}
function AlarmEditor({initial,onSave,onCancel}){
 const [a,setA]=useState({...initial,weekdays:[...(initial.weekdays||[])]});
 const patch=(key,val)=>setA(prev=>({...prev,[key]:val}));
 const preset=(value)=>{
   if(value==='once')setA(prev=>({...prev,repeat:'once'}));
   if(value==='daily')setA(prev=>({...prev,repeat:'weekly',weekdays:[0,1,2,3,4,5,6]}));
   if(value==='weekdays')setA(prev=>({...prev,repeat:'weekly',weekdays:[1,2,3,4,5]}));
   if(value==='weekends')setA(prev=>({...prev,repeat:'weekly',weekdays:[0,6]}));
   if(value==='custom')setA(prev=>({...prev,repeat:'weekly',weekdays:[...(prev.weekdays||[])]}));
 };
 const dayPreset=a.repeat==='once'?'once':a.weekdays.length===7?'daily':JSON.stringify([...a.weekdays].sort())==='[1,2,3,4,5]'?'weekdays':JSON.stringify([...a.weekdays].sort())==='[0,6]'?'weekends':'custom';
 const valid=a.time && a.label.trim() && a.message.trim() && (a.repeat==='once'?a.date>=jakartaDate():a.weekdays.length>0);
 return <div className="modal-backdrop alarm-backdrop" onMouseDown={e=>{if(e.target===e.currentTarget)onCancel()}}><form className="alarm-editor" onSubmit={e=>{e.preventDefault();if(valid)onSave({...a,label:a.label.trim().slice(0,36),message:a.message.trim().slice(0,75)});}}>
  <div className="alarm-editor-heading"><div><span className="eyebrow">JADWAL MINDPAL</span><h2>{a.id?'Edit alarm':'Tambah alarm baru'}</h2></div><button type="button" className="alarm-x" onClick={onCancel} aria-label="Tutup">×</button></div>
  <label className="alarm-form-label">Pukul berapa?</label><input className="alarm-time-input" type="time" required value={a.time} onChange={e=>patch('time',e.target.value)}/>
  <div className="alarm-grid-two"><label className="field">Nama alarm<input required maxLength={36} value={a.label} onChange={e=>patch('label',e.target.value)} placeholder="Contoh: Bangun pagi"/></label><label className="field">Ulangi<select value={dayPreset} onChange={e=>preset(e.target.value)}><option value="daily">Setiap hari</option><option value="weekdays">Senin–Jumat</option><option value="weekends">Sabtu–Minggu</option><option value="custom">Pilih hari</option><option value="once">Sekali saja</option></select></label></div>
  {a.repeat==='once'?<label className="field">Tanggal alarm (WIB)<input type="date" min={jakartaDate()} required value={a.date} onChange={e=>patch('date',e.target.value)}/></label>:<div className="alarm-days" role="group" aria-label="Hari alarm">{WD_ORDER.map(d=><button key={d} type="button" className={(a.weekdays||[]).includes(d)?'active':''} aria-pressed={(a.weekdays||[]).includes(d)} onClick={()=>{const days=(a.weekdays||[]).includes(d)?a.weekdays.filter(x=>x!==d):[...a.weekdays,d];patch('weekdays',days);}}>{DAYS[d]}</button>)}</div>}
  <label className="field">Teks yang tampil di OLED<input required maxLength={75} value={a.message} onChange={e=>patch('message',e.target.value)} placeholder="Wake up! You can do it!"/><small>Hingga 75 karakter; teks panjang akan dibagi beberapa baris.</small></label>
  <div className="alarm-grid-tone"><label className="field">Melodi alarm<select value={a.tone} onChange={e=>patch('tone',Number(e.target.value))}>{TONES.map((t,i)=><option value={i} key={t.name}>{t.name}</option>)}</select></label><button type="button" className="preview" onClick={()=>previewTone(a.tone)}><Music2 size={17}/> Dengarkan</button></div>
  <label className="field">Tunda / snooze<select value={a.snoozeMin} onChange={e=>patch('snoozeMin',Number(e.target.value))}><option value={1}>1 menit</option><option value={5}>5 menit</option><option value={10}>10 menit</option></select></label>
  <div className="alarm-editor-actions"><button className="alarm-cancel" type="button" onClick={onCancel}>Batal</button><button className="alarm-add" type="submit" disabled={!valid}><Check size={17}/> Simpan alarm</button></div>
 </form></div>
}
export default function AlarmManager({alarms,onChange,notice}){
 const [editor,setEditor]=useState(null);const items=Object.values(alarms||{}).filter(a=>a && typeof a==='object' && typeof a.time==='string').sort((a,b)=>a.time.localeCompare(b.time));
 const update=(next)=>onChange(next);
 const save=(alarm)=>{const id=alarm.id||('a'+Date.now().toString(36)+Math.random().toString(36).slice(2,6));update({...alarms,[id]:{...alarm,id}});setEditor(null)};
 const del=(id)=>{if(!window.confirm('Hapus alarm ini?'))return;const next={...alarms};delete next[id];update(next);};
 return <>
 <section className="alarm-intro"><div><span className="eyebrow">YOUR DAILY ROUTINE</span><h2>Bangun, belajar, istirahat. <span>✦</span></h2><p>Atur banyak alarm seperti di HP, dengan jam, hari, nada, dan pesan masing-masing.</p></div><button className="alarm-add" disabled={items.length>=MAX_ALARMS} onClick={()=>setEditor(defaultAlarm())}><Plus size={18}/> Tambah alarm</button></section>
 <div className="alarm-summary"><AlarmClock size={18}/><span><strong>{items.filter(a=>a.enabled).length} aktif</strong> dari {items.length} alarm</span><span className="alarm-timezone">Waktu Indonesia Barat · WIB (UTC+7)</span></div>
 {items.length===0?<div className="alarm-empty"><div className="alarm-empty-icon"><AlarmClock size={35}/></div><h3>Belum ada alarm</h3><p>Tambahkan alarm pertama untuk belajar, bangun pagi, minum air, atau kegiatanmu yang lain.</p><button className="alarm-add" onClick={()=>setEditor(defaultAlarm())}><Plus size={18}/> Buat alarm pertama</button></div>:<div className="alarm-cards">{items.map(a=><article key={a.id} className={'alarm-card '+(!a.enabled?'muted':'')}>
 <div className="alarm-card-top"><div className="alarm-clock"><strong>{a.time}</strong><span>{nextFire(a)}</span></div><button className={'switch '+(a.enabled?'on':'')} type="button" onClick={()=>update({...alarms,[a.id]:{...a,enabled:!a.enabled}})} aria-label={`${a.enabled?'Matikan':'Nyalakan'} alarm ${a.label}`} aria-pressed={a.enabled}><span/></button></div>
 <div className="alarm-card-middle"><h3>{a.label}</h3><p><CalendarDays size={14}/> {describeRepeat(a)}</p><p><Music2 size={14}/> {TONES[a.tone]?.name||TONES[0].name}</p><div className="alarm-message">“{a.message}”</div></div>
 <div className="alarm-card-actions"><button onClick={()=>previewTone(a.tone)}><Volume2 size={15}/> Preview</button><button onClick={()=>setEditor(a)}><Pencil size={15}/> Edit</button><button className="remove" onClick={()=>del(a.id)} aria-label={'Hapus '+a.label}><Trash2 size={15}/></button></div>
 </article>)}</div>}
 <div className="alarm-notice"><Info size={18}/><div><strong>Jadwal disimpan ke Firebase</strong><p>Alarms terjadwal akan berbunyi melalui ESP32 yang menjalankan firmware v8, hidup, punya waktu NTP valid, dan sudah menyinkronkan jadwal. Tanpa ESP32 menyala, website tidak menjamin alarm berbunyi.</p></div></div>
 {editor&&<AlarmEditor initial={editor} onSave={save} onCancel={()=>setEditor(null)}/>}
 </>;
}
