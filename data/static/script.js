'use strict';


const updateInterval = 1000;
const maxLogs = 15;


const WATERING_CHANNEL_READ_KEY = "VE44P8FN0R7QT2S3";
const WATERING_CHANNEL_ID = 2338820;


var current_configs = {};



/** Atualiza informação de tempo real dos sensores */
function updateSensors() {
    fetch("/readsensors").then(res => {
	if (res.ok) {
	    res.text().then(resText => {
		const json = JSON.parse(resText);
		if (json.event === 'read-sensors') {
		    //  console.log((new Date(json.timestamp)).toLocalTimeString()
		    // (new Date(Date.now())).toLocaleString(undefined, {timeZoneName: 'shortGeneric'})
		    ['temp', 'hum', 'soil', 'lumi'].forEach(id => {
			const ele = document.querySelector(`#${id}`);
			const reading = json.data[id];
			ele.textContent = reading ?
			    reading.toLocaleString("pt-br",
						   {
						       useGrouping:false,
						       minimumIntegerDigits: 2,
						       minimumFractionDigits: 1,
						       maximumFractionDigits: 2,
						   }) :
			    "--ERR--";
		    });
		} else {
		    console.error("Evento de leitura errado.");
		}
	    });
	} else {
	    console.error("Erro na leitura dos sensores");
	}
    }).catch(err => {
	console.error(`Erro no fetch de leitura de sensores: ${err.message}`);
    });
}

setInterval(updateSensors, updateInterval);



/** Executa função depois da página ter carregado. */
function onReady(fun) {
    if (document.readyState != "loading") {
	fun();
    } else {
	document.addEventListener("DOMContentLoaded", fun);
    }
}





// WebSockets
const websocket = new WebSocket(`ws://${window.location.hostname}/websocket`);


onReady(function () {
    const soquet_data = document.querySelector('#socket-data');
    const water_now_btn = document.querySelector('#water-now-btn');


    // Animações
    const btn_def_text = water_now_btn.textContent;
    const btn_doing_text = "Regando";
    const btn_wait_text = "Aguarde";

    let doing_interval = undefined;
    let wait_interval = undefined;
    
    
    function add_ellip(str, tick) {
        return str + ".".repeat(tick % 4); //de "" até "..."
    }

    function anim_ellip(str) {
        let count = 1; //para começar com ".", 
        return function() {
            return add_ellip(str, count++);
        };
    }    
    
    //Eventos websocket
    websocket.addEventListener('message', function(ev) {
	try {
	    console.log(`Evento: ${ev.data}`);
	    
	    const json_data = JSON.parse(ev.data);
	    if        (json_data.type === "log") {
		addInLogWindow(json_data.message);
		
	    } else if (json_data.type === "enable-water-now") {
		water_now_btn.textContent = btn_def_text;
		water_now_btn.removeAttribute("disabled");
		clearInterval(doing_interval);				
		clearInterval(wait_interval);
		doing_interval = undefined;
		wait_interval = undefined;
		
	    } else if (json_data.type === "disable-water-now") {
		water_now_btn.setAttribute("disabled", "disabled");
		water_now_btn.textContent = btn_doing_text;
		
		let watering_time = current_configs["watering_time"] * 1000;
		let doing_anim = anim_ellip(btn_doing_text);
		
		doing_interval = setInterval(function() {
		    water_now_btn.textContent = doing_anim();
		}, 1000);
		
		setTimeout(function() {
		    clearInterval(doing_interval);
		    doing_interval = undefined;
		    
		    let wait_anim = anim_ellip(btn_wait_text);
		    wait_interval = setInterval(function() {
			water_now_btn.textContent = wait_anim();
			loadWateringLogTable();
			
		    }, 1000);
		}, watering_time);
		
		
		

	    } else if (json_data.type === "reload-config") {
		loadConfigsForm();
		
	    } else {
		console.log(`Tipo de mensagem websocket desconhecida: “${json_data.type}”.`);
	    }
	} catch (ex) {
	    console.log(`Evento websocket não Json: ${ev.data}.`);
	}
    });

    water_now_btn.addEventListener('click', ev => {
	websocket.send(JSON.stringify({
	    type: "water-now-btn",
	    message: null,
	}));
    });
});




function addInLogWindow(message) {
    const span_timestamp = document.createElement("span");
    span_timestamp.classList.add("timestamp");
    span_timestamp.textContent = (new Date()).toISOString()

    const span_log_message = document.createElement("span");
    span_timestamp.classList.add("log-message");    
    span_log_message.textContent = message;

    const p = document.createElement("p");
    p.appendChild(span_timestamp);
    p.appendChild(span_log_message);
    
    const div_logs =  document.querySelector("div.logs");
    if (div_logs.children.length > maxLogs) {
	const ch = div_logs.children[0]
	ch.remove();
    }

    div_logs.insertBefore(p, null);    
}


async function readWateringLog() {
    const res = await fetch(`https://api.thingspeak.com/channels/${WATERING_CHANNEL_ID}/feeds.json?api_key=${WATERING_CHANNEL_READ_KEY}&results=20`);
    const res_data = await res.json();
    res_data.feeds.reverse();
    return res_data;
}




async function loadWateringLogTable() {
    const regas = await readWateringLog();
    
    const regas_tbl_tbody = document.querySelector("#regas-tbl-tbody");
    regas_tbl_tbody.replaceChildren([]);

    
    regas.feeds.forEach(rega => {
	const tr = document.createElement("tr");
	const td = Array(3)
	      .fill(null)
	      .map(() => document.createElement("td"));
	
	td[0].textContent = (new Date(rega.field1)).toLocaleString();
	td[1].textContent = {
	    "water-now": "Regar agora",
	    "low-soil-humi": "Umidade baixa",
	    "alarm":"Relógio",
	    "none":"???"}[rega.field3];
	td[2].textContent = `${rega.field2 / 1000}s`;
	

	tr.replaceChildren(...td);
	regas_tbl_tbody.appendChild(tr);
	
    });
}

onReady(loadWateringLogTable);





/** Carrega configurações do servidor para current_configs.
    Utilizado para inicialização do formulário e para comparar alterações
    com a configuração do ESP32. */
async function readConfigs() {
    const res = await fetch(`http://${window.location.hostname}/configs`);
    const res_json = await res.json();
    const configs = res_json.configs;

    current_configs = configs;

    return configs;
}








let form_ids = [
    "sens_log_delay",
    "watering_time",
    
    "check_low_soil_humi",
    "check_low_soil_humi_interval",
    "min_soil_humi",

    "activate_alarm",
    "alarm_hours_alarm_minutes",
]

let form_elements = {};

// Campos dependentes de checkbox no formulário
let check_low_soil_humi_forms = [];
let activate_alarm_forms = [];


    

// inicializa form_elements e dependentes
onReady(function() {
    form_elements = {};
    form_ids.forEach(function (id) {
	form_elements[id] = document.getElementById(id);
    });

    check_low_soil_humi_forms = [
	form_elements.check_low_soil_humi_interval,
	form_elements.min_soil_humi,
    ];

    activate_alarm_forms = [
	form_elements.alarm_hours_alarm_minutes,
    ];	
});











let form_configs;

/** Carrega valores atuais do formulário para a variável form_configs.
    Usado para comparar com os valores definidos atualmente no ESP32. */
function reload_form_configs() {
    form_configs = {
	sens_log_delay:            parseInt(form_elements["sens_log_delay"].value),
	watering_time:             parseInt(form_elements["watering_time"].value),
	
	check_low_soil_humi:       form_elements["check_low_soil_humi"].checked,
        check_low_soil_humi_interval: parseInt(form_elements["check_low_soil_humi_interval"].value),
        min_soil_humi:             parseInt(form_elements["min_soil_humi"].value).toFixed(1),
	
	activate_alarm:            form_elements["activate_alarm"].checked,
	alarm_hours_alarm_minutes: form_elements["alarm_hours_alarm_minutes"].value,
    };

    return form_configs;
}



/** Carrega configurações do ESP32 para o formulário.
    Usado quando a página é carregada e quando as configurações
    são desfeitas. */
async function loadConfigsForm() {
    disableForm();
    
    const configs = await readConfigs();
    Object.values(form_elements).forEach(function (element) {
	 element.value = configs[element.id];
    });

    form_elements.alarm_hours_alarm_minutes.value =
	to_time_input_value(configs.alarm_hours, configs.alarm_minutes);

    form_elements.activate_alarm.checked = configs.activate_alarm;
    form_elements.check_low_soil_humi.checked = configs.check_low_soil_humi;
    form_elements.min_soil_humi.value = configs.min_soil_humi.toFixed(1);

    

    enableForm();
    
    form_configs = {...configs};
}


/** Desabilita todos os campos do formulário.
    Utilizado para recaregar configurações do servidor. */
function disableForm() {
    Object.values(form_elements).forEach(disable);
}



/** Habilita todos os campos do formulário.
    Menos os com os campos checkbox não selecionados
    Utilizado depois do carregamento do servidor */    
function enableForm() {
    Object.values(form_elements).forEach(enable);
    if (!form_elements.check_low_soil_humi.checked) {
	disable(form_elements.check_low_soil_humi_interval);
	disable(form_elements.min_soil_humi);
    }

    if (!form_elements.activate_alarm.checked) {
	disable(alarm_hours_alarm_minutes);
    }


					 
}


/** Modifica form_config para ter o mesmo formato de
    current_config.
    Usado para compara com configuração anterior e para
    salvar no servidor */
function adjust_form_configs() {
    let adjust_time =  {...form_configs};

    const time_alarm = form_elements.alarm_hours_alarm_minutes;

    
    adjust_time["alarm_hours"] = time_value_hours(time_alarm);
    adjust_time["alarm_minutes"] = time_value_minutes(time_alarm);

    delete adjust_time.alarm_hours_alarm_minutes;

    return adjust_time;
}


function comp_configs() {
    reload_form_configs();

    let adjust_configs = adjust_form_configs();
    
    return Object.keys(adjust_configs).every(key =>
	adjust_configs[key] == current_configs[key]);
}





/** Adiciona eventos de formulário */
onReady(function () {
    //Desativa dependentes de checkbox
    form_elements.check_low_soil_humi.addEventListener('change', function() {
	if (!this.checked) {
	    check_low_soil_humi_forms.forEach(ele => {
		disable(ele);
		ele.value = current_configs[ele.id].toFixed(1);;
	    });	    
	} else {
	    check_low_soil_humi_forms.forEach(enable);
	}
    });

    form_elements.activate_alarm.addEventListener('change', function() {
	if(!this.checked) {
	    disable(form_elements.alarm_hours_alarm_minutes);
	    form_elements.alarm_hours_alarm_minutes.value =
		to_time_input_value(current_configs.alarm_hours, current_configs.alarm_minutes);
	} else {
	    enable(form_elements.alarm_hours_alarm_minutes);
	}
    });

		
	    

    //Altera os botões quando o formulário mudar
    const undo_btn = document.getElementById("undo-btn");
    const save_configs = document.getElementById("save-configs");
    const configs_form = document.getElementById("configs-form");

    configs_form.addEventListener('change', function () {
	if (!comp_configs()) {
	    enable(undo_btn);
	    enable(save_configs);
	} else {
	    disable(undo_btn);
	    disable(save_configs);
	}	
    });

    //Desfazer mudanças (recarrega formulário)
    undo_btn.addEventListener('click', loadConfigsForm);
});






/** Converte horas e minutos para "hh:mm" para usar no campo value do input tipo time. */
function to_time_input_value(hours, min) {
    return `${String(hours).padStart(2,"0")}:${String(min).padStart(2,"0")}`;
}

/** Números de horas na input tipo time. */
function time_value_hours(el) {
    return el.valueAsDate.getUTCHours();
}

/** Números de minutos no input tipo time. */
function time_value_minutes(el) {
    return el.valueAsDate.getUTCMinutes();    
}


/** Habilita elemento. */
function enable(elm) {
    elm.removeAttribute("disabled");
}

/** Dehabilita elemento. */
function disable(elm) {
    elm.setAttribute("disabled", "disabled");
}

