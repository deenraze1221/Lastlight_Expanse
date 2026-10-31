// Ten original procedural scores. Each has its own harmony, melody, tempo and timbre.
export const MUSIC_TRACKS={
 calm:[
 {name:'Afterglow',bpm:54,wave:'sine',cutoff:650,chords:[[48,52,55],[45,48,52],[50,53,57],[43,48,52]],melody:[67,64,62,60,64,67,69,64]},
 {name:'Empty Streets',bpm:66,wave:'triangle',cutoff:520,chords:[[45,48,52],[41,45,48],[48,52,55],[43,47,50]],melody:[64,60,57,60,65,64,60,59]},
 {name:'Safe Light',bpm:48,wave:'sine',cutoff:900,chords:[[50,54,57],[47,50,54],[43,47,50],[45,49,52]],melody:[69,66,62,66,71,69,66,64]},
 {name:'Cold Dawn',bpm:52,wave:'sine',cutoff:580,chords:[[46,49,53],[42,46,49],[44,48,51],[41,44,48]],melody:[65,61,58,60,65,68,65,60]},
 {name:'Last Shelter',bpm:60,wave:'triangle',cutoff:700,chords:[[43,46,50],[39,43,46],[44,48,51],[41,45,48]],melody:[62,58,55,58,63,62,58,57]}],
 tense:[
 {name:'Pressure',bpm:104,wave:'triangle',cutoff:480,chords:[[33,36,40],[29,33,36],[31,35,38],[28,31,35]],melody:[57,57,60,59,57,64,60,56]},
 {name:'Pursuit',bpm:126,wave:'sawtooth',cutoff:320,chords:[[38,41,45],[36,39,43],[34,38,41],[33,37,40]],melody:[62,65,62,69,68,65,61,62]},
 {name:'Hive Pulse',bpm:114,wave:'square',cutoff:270,chords:[[31,34,38],[30,34,37],[32,35,39],[29,32,36]],melody:[55,56,58,55,62,61,58,54]},
 {name:'Blackout',bpm:118,wave:'square',cutoff:390,chords:[[34,37,41],[30,34,37],[33,36,40],[31,34,38]],melody:[58,61,58,65,64,61,57,58]},
 {name:'Siege',bpm:136,wave:'triangle',cutoff:620,chords:[[36,39,43],[32,36,39],[35,38,42],[33,36,40]],melody:[60,63,67,66,63,62,59,60]}]
};
export const noteHz=n=>440*2**((n-69)/12);
