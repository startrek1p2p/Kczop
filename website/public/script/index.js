import { initializeApp } from "https://www.gstatic.com/firebasejs/11.6.0/firebase-app.js";
import { getDatabase, ref, onValue, set, remove, query, orderByKey, limitToLast, onChildAdded, endAt, get } from "https://www.gstatic.com/firebasejs/11.6.0/firebase-database.js";
import { createTemperatureChart, createHumidityChart } from "./charts-definition.js";
import { createTemperatureGauge, createHumidityGauge } from "./gauges-definition.js";

// Firebase configuration
// Your web app's Firebase configuration
// For Firebase JS SDK v7.20.0 and later, measurementId is optional
const firebaseConfig = {
    apiKey: "AIzaSyC454ZiHeXjjwMkIYqdZrABAMnzZ30-rmQ",
    authDomain: "kczop-551b1.firebaseapp.com",
    projectId: "kczop-551b1",
    storageBucket: "kczop-551b1.firebasestorage.app",
    messagingSenderId: "339602662416",
    appId: "1:339602662416:web:44f320e7218680525b5695",
    measurementId: "G-TQ99CDYGLC"
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const database = getDatabase(app);

// Fixed database path now that authentication is removed.
// Update DATA_USER_PATH to point to the desired user node in your database.
const DATA_USER_PATH = "UsersData/public";
const READINGS_PATH = `${DATA_USER_PATH}/readings`;
const CHART_RANGE_PATH = `${DATA_USER_PATH}/charts/range`;

const epochToJsDate = (epochTime) => new Date(epochTime * 1000);

const epochToDateTime = (epochTime) => {
    if (!epochTime) return 'N/A';
    const date = epochToJsDate(epochTime);
    return `${date.getFullYear()}/${String(date.getMonth() + 1).padStart(2, '0')}/${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`;
};

// Validate and format sensor reading
const formatReading = (value) => (typeof value === 'number' ? value.toFixed(2) : 'N/A');

const plotValues = (chart, timestamp, value) => {
    if (!timestamp || typeof value !== 'number') return;
    const x = epochToJsDate(timestamp).getTime();
    const y = Number(value);
    const shift = chart.series[0].data.length > 40;
    chart.series[0].addPoint([x, y], true, shift, true);
};

// DOM elements
const contentElement = document.querySelector('#content-sign-in');
const deleteButtonElement = document.getElementById('delete-button');
const deleteModalElement = document.getElementById('delete-modal');
const deleteDataFormElement = document.querySelector('#delete-data-form');
const viewDataButtonElement = document.getElementById('view-data-button');
const hideDataButtonElement = document.getElementById('hide-data-button');
const tableContainerElement = document.querySelector('#table-container');
const chartsRangeInputElement = document.getElementById('charts-range');
const loadDataButtonElement = document.getElementById('load-data');
const cardsCheckboxElement = document.querySelector('input[name=cards-checkbox]');
const gaugesCheckboxElement = document.querySelector('input[name=gauges-checkbox]');
const chartsCheckboxElement = document.querySelector('input[name=charts-checkbox]');
const cardsReadingsElement = document.querySelector('#cards-div');
const gaugesReadingsElement = document.querySelector('#gauges-div');
const chartsDivElement = document.querySelector('#charts-div');
const tempElement = document.getElementById('temp');
const humElement = document.getElementById('hum');
const updateElement = document.getElementById('lastUpdate');

// Chart and Gauge variables
let chartT, chartH;
let gaugeT, gaugeH;

const initDashboard = () => {
    const dbRef = ref(database, READINGS_PATH);
    const chartRef = ref(database, CHART_RANGE_PATH);

    // Always show the dashboard now that auth is removed
    contentElement.style.display = 'block';

    // Initialize gauges
    gaugeT = createTemperatureGauge();
    gaugeH = createHumidityGauge();
    gaugeT.draw();
    gaugeH.draw();

    // Charts
    onValue(chartRef, (snapshot) => {
        const rawValue = snapshot.val();
        // Ensure chartRange is a positive integer; default to 100 if invalid
        const chartRange = Number.isInteger(Number(rawValue)) && Number(rawValue) > 0 ? Number(rawValue) : 100;

        // Destroy existing charts
        if (chartT) chartT.destroy();
        if (chartH) chartH.destroy();

        // Create new charts
        chartT = createTemperatureChart();
        chartH = createHumidityChart();

        // Query and plot latest readings
        const readingsQuery = query(dbRef, orderByKey(), limitToLast(chartRange));
        onChildAdded(readingsQuery, (snapshot) => {
            const data = snapshot.val();
            console.log('Chart reading:', data);
            if (data && typeof data === 'object') {
                const { temperature, humidity, timestamp } = data;
                plotValues(chartT, timestamp, temperature);
                plotValues(chartH, timestamp, humidity);
            }
        });
    });

    // Update chart range
    chartsRangeInputElement.addEventListener('change', () => {
        const newValue = Number(chartsRangeInputElement.value);
        // Only update if the new value is a positive integer
        if (Number.isInteger(newValue) && newValue > 0) {
            set(chartRef, newValue);
        } else {
            console.warn('Invalid chart range input; must be a positive integer');
            chartsRangeInputElement.value = ''; // Clear invalid input
        }
    });

    // Checkboxes
    cardsCheckboxElement.addEventListener('change', () => {
        cardsReadingsElement.style.display = cardsCheckboxElement.checked ? 'block' : 'none';
    });

    gaugesCheckboxElement.addEventListener('change', () => {
        gaugesReadingsElement.style.display = gaugesCheckboxElement.checked ? 'block' : 'none';
    });

    chartsCheckboxElement.addEventListener('change', () => {
        chartsDivElement.style.display = chartsCheckboxElement.checked ? 'block' : 'none';
    });

    // Cards
    const lastReadingQuery = query(dbRef, orderByKey(), limitToLast(1));
    onChildAdded(lastReadingQuery, (snapshot) => {
        const data = snapshot.val();
        console.log('Card reading:', data);
        if (data && typeof data === 'object') {
            const { temperature, humidity, timestamp } = data;
            tempElement.innerHTML = formatReading(temperature);
            humElement.innerHTML = formatReading(humidity);
            updateElement.innerHTML = epochToDateTime(timestamp);
        } else {
            tempElement.innerHTML = 'N/A';
            humElement.innerHTML = 'N/A';
            updateElement.innerHTML = 'N/A';
        }
    });

    // Gauges
    onChildAdded(lastReadingQuery, (snapshot) => {
        const data = snapshot.val();
        console.log('Gauge reading:', data);
        if (data && typeof data === 'object') {
            const { temperature, humidity, timestamp } = data;
            gaugeT.value = typeof temperature === 'number' ? temperature : 0;
            gaugeH.value = typeof humidity === 'number' ? humidity : 0;
            updateElement.innerHTML = epochToDateTime(timestamp);
        }
    });

    // Delete Data
    deleteButtonElement.addEventListener('click', (e) => {
        e.preventDefault();
        deleteModalElement.style.display = 'block';
    });

    deleteDataFormElement.addEventListener('submit', (e) => {
        e.preventDefault();
        remove(dbRef);
        deleteModalElement.style.display = 'none';
        // Reset UI after deletion
        tempElement.innerHTML = 'N/A';
        humElement.innerHTML = 'N/A';
        updateElement.innerHTML = 'N/A';
        gaugeT.value = 0;
        gaugeH.value = 0;
    });

    // Table
    let lastReadingTimestamp;
    const createTable = () => {
        const tableQuery = query(dbRef, orderByKey(), limitToLast(100));
        let firstRun = true;
        onChildAdded(tableQuery, (snapshot) => {
            const data = snapshot.val();
            console.log('Table reading:', data);
            if (data && typeof data === 'object') {
                const { temperature, humidity, timestamp } = data;
                const content = `
            <tr>
              <td>${epochToDateTime(timestamp)}</td>
              <td>${formatReading(temperature)}</td>
              <td>${formatReading(humidity)}</td>
            </tr>`;
                $('#tbody').prepend(content);
                if (firstRun && timestamp) {
                    lastReadingTimestamp = timestamp;
                    firstRun = false;
                }
            }
        });
    };

    const appendToTable = async () => {
        const tableQuery = query(dbRef, orderByKey(), limitToLast(100), endAt(String(lastReadingTimestamp)));
        const snapshot = await get(tableQuery);
        const dataList = [];
        snapshot.forEach((child) => {
            const data = child.val();
            console.log('Append table reading:', data);
            if (data && typeof data === 'object') {
                dataList.push(data);
            }
        });
        if (dataList.length > 0) {
            lastReadingTimestamp = dataList[0].timestamp;
            const reversedList = dataList.reverse();
            reversedList.forEach((element, index) => {
                if (index === 0) return; // Skip first reading
                const { temperature, humidity, timestamp } = element;
                const content = `
            <tr>
              <td>${epochToDateTime(timestamp)}</td>
              <td>${formatReading(temperature)}</td>
              <td>${formatReading(humidity)}</td>
            </tr>`;
                $('#tbody').append(content);
            });
        }
    };

    viewDataButtonElement.addEventListener('click', () => {
        tableContainerElement.style.display = 'block';
        viewDataButtonElement.style.display = 'none';
        hideDataButtonElement.style.display = 'inline-block';
        loadDataButtonElement.style.display = 'inline-block';
        createTable();
    });

    loadDataButtonElement.addEventListener('click', appendToTable);

    hideDataButtonElement.addEventListener('click', () => {
        tableContainerElement.style.display = 'none';
        viewDataButtonElement.style.display = 'inline-block';
        hideDataButtonElement.style.display = 'none';
        loadDataButtonElement.style.display = 'none';
    });

    // Initialize charts/range if it doesn't exist
    get(chartRef).then((snapshot) => {
        if (!snapshot.exists()) {
            set(chartRef, 100); // Set default chart range
            console.log('Initialized charts/range to 100');
        }
    });
};

document.addEventListener('DOMContentLoaded', initDashboard);
