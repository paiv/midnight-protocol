function binaryPlayer(memorySize=256) {
    const wasmSource = 'arac.wasm'
    async function instantiate() {
        let imports = {env:{memory:_memory}, host:{
            time_now:() => performance.now(),
            random:() => Math.random(),
            sqrlog:(x,y) => Math.sqrt(Math.log(x) / y),
            trace_log:(x) => console.log(x)}}
        if (_module !== undefined) {
            _instance = await WebAssembly.instantiate(_module, imports)
        }
        else if ('instantiateStreaming' in WebAssembly) {
            let res = await WebAssembly.instantiateStreaming(fetch(wasmSource), imports)
            _module = res.module
            _instance = res.instance
        }
        else {
            _module = await fetch(wasmSource).then(res => res.arrayBuffer()).then(data => WebAssembly.compile(data))
            _instance = await WebAssembly.instantiate(_module, imports)
        }
        encodeConfig()
        _instance.exports.setup()
    }
    function encodeConfig() {
        let memorySize = _memory.buffer.byteLength / 0x10000 // pages
        let timeLimit = 2000 // ms
        let difficultyLevel = _level || 0
        let data = [memorySize, timeLimit, difficultyLevel]
        let mem = new Uint32Array(_memory.buffer)
        for (let [i,x] of data.entries()) {
            mem[i] = x
        }
    }
    function encodeState(state) {
        let board = new Array()
        for (let y = 0; y < 50; y += 10) {
            for (let x = 0; x < 5; ++x) {
                board.push(state.board.get(y+x) || 0)
            }
        }
        let data = [state.currentPlayer, ...board, ...state.progs]
        let mem = new Uint8Array(_memory.buffer)
        for (let [i,x] of data.entries()) {
            mem[i] = x
        }
    }
    function decodeMove() {
        let move = new Array()
        let mem = new Uint8Array(_memory.buffer)
        for (let i = 1; i < 4; ++i) {
            move.push(mem[i])
        }
        return move
    }
    async function inner(state) {
        encodeState(state)
        let r = _instance.exports.select_move()
        if (r) {
            r = decodeMove()
        }
        return Promise.resolve(r)
    }
    let _uid = 0, _level = undefined
    let _state = undefined
    let _module=undefined, _brain=undefined
    let _memory = new WebAssembly.Memory({initial:memorySize, maximum:memorySize}) // in pages
    async function init(state, uid, level) {
        _uid = uid
        _state = state
        _level = level
        await instantiate()
    }
    async function update(state, move) {
        _state = state
    }
    async function getmove() {
        try {
            return await inner(_state)
        }
        catch (e) {
            console.log(e);
        }
    }
    function discard() {
    }
    return {init, update, getmove, discard}
}


const Player = binaryPlayer()


async function dispatch(name, ...args) {
    if (name in Player) {
        let f = Player[name]
        let r = f(...args)
        if (r instanceof Promise) {
            r = await r
        }
        return [name, r]
    }
}


onmessage = async function (e) {
    let r = await dispatch(...e.data)
    postMessage(r)
}
