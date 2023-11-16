'use strict';



function onReady(fun) {
    if (document.readyState != "loading") {
	fun();
    } else {
	document.addEventListener("DOMContentLoaded", fun);
    }
}


const maxData = 35;


function dateNow() {
    return Date.now().valueOf();
}


const tempCanv = document.querySelector('#temp-chart');
const humCanv = document.querySelector('#hum-chart');
const soilCanv = document.querySelector('#soil-chart');
const lumiCanv = document.querySelector('#lumi-chart');
	

const humChartData = {
    labels: [
    ],
    datasets: [{
	label: 'Umidade',
	fill: false,
	backgroudnColor: 'rgba(255, 99, 132, 0.5)',	
	borderColor: '#54E668',
	data: [],
    }],
}

const soilChartData = {
    labels: [
    ],
    datasets: [{
	label: 'Umidade do solo',
	fill: false,
	backgroudnColor: 'rgba(255, 99, 132, 0.5)',	
	borderColor: '#E6BE3E',
	data: [],
    }],
}

const lumiChartData = {
    labels: [
    ],
    datasets: [{
	label: 'Luminosidade',
	fill: false,
	backgroudnColor: 'rgba(255, 99, 132, 0.5)',	
	borderColor: '#49E6C9',
	data: [],
    }],
}

const tempChartData = {
    labels: [
    ],
    datasets: [{
	label: 'Temperatura',
	fill: false,
	backgroudnColor: '#rgba(255, 99, 132, 0.5)',	
	borderColor: '#E63E6A',
	data: [],
    }],
}


const humChartConfig = {
    type: 'line',
    data: humChartData,
    options: {
	responsive: true,
	mantainAspectRatio: false,
	aspectRatio: 600/500,	
	plugins: {
	    title: {
		text: 'Umidade do ar'.padEnd(20, " "),
		display: true,
	    },
	    tooltip: {
		callbacks: {
		    afterLabel: val => `${val.raw.toFixed(2)}%`,
		},
	    },
	},
	scales: {
	    x: {
		type: 'time',
		time: {
		    unit: 'second',
		    tooltipFormat: 'dd/mm/yyyy TTT',
		    displayFormats: {
		    },
		},
		title: {
		    display: true,
		    text: 'Horário',
		},
		ticks: {
		    source: 'data',
		},		    
	    },
	    y: {
		display: true,
		text: 'value',
		beginAtZero: true,
		max: 100,
		ticks: {
		    callback: val => `${val.toFixed(2)}%`,
		    format: {
			//style: 'percent',
		    },
		},
	    },
	},
    },
}

const soilChartConfig = {
    type: 'line',
    data: soilChartData,
    options: {
	responsive: true,
	mantainAspectRatio: false,
	aspectRatio: 600/500,	
	plugins: {
	    title: {
		text: 'Umidade do solo'.padEnd(20, " "),
		display: true,
	    },
	    tooltip: {
		callbacks: {
		    afterLabel: val => `${val.raw.toFixed(2)}%`,
		},
	    },
	},
	scales: {
	    x: {
		type: 'time',
		time: {
		    unit: 'second',
		    tooltipFormat: 'dd/mm/yyyy TTT',
		    displayFormats: {
		    },
		},
		title: {
		    display: true,
		    text: 'Horário',
		},
		ticks: {
		    source: 'data',
		},		    
	    },
	    y: {
		display: true,
		text: 'value',
		beginAtZero: true,
		max: 100,
		ticks: {
		    callback: val => `${val.toFixed(2)}%`,
		    format: {
			//style: 'percent',
		    },
		},
	    },
	},
    },
}


const lumiChartConfig = {
    type: 'line',
    data: lumiChartData,
    options: {
	responsive: true,
	mantainAspectRatio: false,
	aspectRatio: 600/500,
	plugins: {
	    title: {
		text: 'Umidade do ar'.padEnd(20, " "),
		display: true,
	    },
	    tooltip: {
		callbacks: {
		    afterLabel: val => `${val.raw.toFixed(2)}%`,
		},
	    },
	},
	scales: {
	    x: {
		type: 'time',
		time: {
		    unit: 'second',
		    tooltipFormat: 'dd/mm/yyyy TTT',
		    displayFormats: {
		    },
		},
		title: {
		    display: true,
		    text: 'Horário',
		},
		ticks: {
		    source: 'data',
		},		    
	    },
	    y: {
		display: true,
		text: 'value',
		beginAtZero: true,
		max: 100,
		ticks: {
		    callback: val => `${val.toFixed(2)}%`,
		    format: {
			//style: 'percent',
		    },
		},
	    },
	},
    },
}




const tempChartConfig = {
    type: 'line',
    data: tempChartData,
    options: {
	responsive: true,
	mantainAspectRatio: false,
	aspectRatio: 600/500,		
	plugins: {
	    title: {
		text: 'Temperatura'.padEnd(20, " "),
		display: true,
	    },
	    tooltip: {
		callbacks: {
		    afterLabel: val => `${val.raw.toFixed(2)} °C`,
		},
	    },
	},
	scales: {
	    x: {
		type: 'time',
		time: {
		    unit: 'second',
		    tooltipFormat: 'dd/mm/yyyy TTT',
		    displayFormats: {
		    },
		},
		title: {
		    display: true,
		    text: 'Horário',
		},
		ticks: {
		    source: 'data',
		},		    
	    },
	    y: {
		display: true,
		text: 'value',
		//beginAtZero: true,
		//max: 100,
		ticks: {
		    callback: val => `${val.toFixed(2)} °C`,
		    format: {
			//style: 'percent',
		    },
		},
	    },
	},
    },
}






const humChart = new Chart(humCanv, humChartConfig);
const soilChart = new Chart(soilCanv, soilChartConfig);
const lumiChart = new Chart(lumiCanv, lumiChartConfig);
const tempChart = new Chart(tempCanv, tempChartConfig);




function pushData(chart, data) {
    let labels = chart.data.labels;
    let dataset_data = chart.data.datasets[0].data;
    
    if (chart.data.labels.length > maxData) {
	labels.shift();
	dataset_data.shift();
    }

    labels.push(dateNow());
    dataset_data.push(data);

    chart.update();
}




async function updateGraph() {
    const res = await fetch("/readsensors");
    
    if (res.ok) {
	const json = await res.json();
	if (json.event === 'read-sensors') {

	    const data = json.data;

	    console.log(data);

	    pushData(tempChart, data["temp"]);	    	    
	    pushData(humChart, data["hum"]);
	    pushData(soilChart, data["soil"]);
	    pushData(lumiChart, data["lumi"]);	    
	    

	    Object.entries(json.data).forEach(entry => {
		let sens = entry[0];
		let reading = !entry[1] ?
		    NaN :
		    entry[1].toLocaleString("pt-br",
					   {
					       useGrouping:false,
					       minimumIntegerDigits: 2,
					       minimumFractionDigits: 1,
					       maximumFractionDigits: 2,
					   });
				
		console.log(`${sens}:${reading}`);

	    });
	    
	} else {
	    console.error("Evento de leitura errado.");
	}
    } else {
	console.error("Erro na leitura dos sensores");
    }


}
    
function main() {
    'use stsrict';

    const updateInterval = 1000;
    const maxLogs = 30;


    setInterval(updateGraph, updateInterval);

    
}

onReady(main);
