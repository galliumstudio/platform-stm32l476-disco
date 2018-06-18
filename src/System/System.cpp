/*******************************************************************************
 * Copyright (C) Gallium Studio LLC. All rights reserved.
 *
 * This program is open source software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Alternatively, this program may be distributed and modified under the
 * terms of Gallium Studio LLC commercial licenses, which expressly supersede
 * the GNU General Public License and are specifically designed for licensees
 * interested in retaining the proprietary status of their code.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Contact information:
 * Website - https://www.galliumstudio.com
 * Source repository - https://github.com/galliumstudio
 * Email - admin@galliumstudio.com
 ******************************************************************************/

#include "fw_log.h"
#include "fw_assert.h"
#include "app_hsmn.h"
#include "periph.h"
#include "UartActInterface.h"
#include "UartOutInterface.h"
#include "System.h"
#include "SystemInterface.h"
#include "SampleInterface.h"
#include "BtnInterface.h"
#include "LedInterface.h"
#include "bsp.h"

FW_DEFINE_THIS_FILE("System.cpp")

namespace APP {

static char const * const timerEvtName[] = {
    "STATE_TIMER",
    "TEST_TIMER",
    "SPEED_TIMER",
    "RUN_TIMER",
    "REST_TIMER"
};

static char const * const internalEvtName[] = {
    "DONE",
    "RESTART",
};

static char const * const interfaceEvtName[] = {
    "SYSTEM_START_REQ",
    "SYSTEM_START_CFM",
    "SYSTEM_STOP_REQ",
    "SYSTEM_STOP_CFM",
};

void System::SetSpeed(bool forward, uint32_t speed) {
    Evt *evt;
    if (forward) {
        evt = new LedLevelReq(LED0, GetHsm().GetHsmn(), GetHsm().GenSeq(), speed);
        Fw::Post(evt);
        evt = new LedLevelReq(LED1, GetHsm().GetHsmn(), GetHsm().GenSeq(), 0);
        Fw::Post(evt);
    } else {
        evt = new LedLevelReq(LED1, GetHsm().GetHsmn(), GetHsm().GenSeq(), speed);
        Fw::Post(evt);
        evt = new LedLevelReq(LED0, GetHsm().GetHsmn(), GetHsm().GenSeq(), 0);
        Fw::Post(evt);
    }
}

System::System() :
    Active((QStateHandler)&System::InitialPseudoState, SYSTEM, "SYSTEM",
           timerEvtName, ARRAY_COUNT(timerEvtName),
           internalEvtName, ARRAY_COUNT(internalEvtName),
           interfaceEvtName, ARRAY_COUNT(interfaceEvtName)),
    m_uart2OutFifo(m_uart2OutFifoStor, UART_OUT_FIFO_ORDER),
    m_uart2InFifo(m_uart2InFifoStor, UART_IN_FIFO_ORDER),
    m_forward(true), m_speed(0), m_runTime(MAX_RUNTIME),
    m_stateTimer(this->GetHsm().GetHsmn(), STATE_TIMER),
    m_testTimer(this->GetHsm().GetHsmn(), TEST_TIMER),
    m_speedTimer(this->GetHsm().GetHsmn(), SPEED_TIMER),
    m_runTimer(this->GetHsm().GetHsmn(), RUN_TIMER),
    m_restTimer(this->GetHsm().GetHsmn(), REST_TIMER) {}

QState System::InitialPseudoState(System * const me, QEvt const * const e) {
    (void)e;
    return Q_TRAN(&System::Root);
}

QState System::Root(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            // Test only
            Periph::SetupNormal();
            Evt *evt;
            evt = new UartActStartReq(UART2_ACT, GET_HSMN(), GEN_SEQ(), &me->m_uart2OutFifo, &me->m_uart2InFifo);
            me->GetHsm().SaveOutSeq(*evt);
            Fw::Post(evt);

            //me->m_testTimer.Start(500, Timer::PERIODIC);
            //evt = new SampleStartReq(SAMPLE, SYSTEM, 0);
            //Fw::Post(evt);

            // Test only for ATWINC1500 startup test.
            GPIO_InitTypeDef  GPIO_InitStruct;
            __HAL_RCC_GPIOE_CLK_ENABLE();
            GPIO_InitStruct.Pin = GPIO_PIN_14;
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Pull = GPIO_PULLUP;
            GPIO_InitStruct.Speed = GPIO_SPEED_FAST;
            HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
            GPIO_InitStruct.Pin = GPIO_PIN_15;
            HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_15, GPIO_PIN_RESET);

            me->m_forward = true;
            me->m_runTime = MAX_RUNTIME;
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            // Test only.
            me->m_testTimer.Stop();
            return Q_HANDLED();
        }
        case Q_INIT_SIG: {
            EVENT(e);
            return Q_TRAN(&System::Idle);
        }
        case TEST_TIMER: {
            //EVENT(e);
            // Test only.
            static int testcount = 10000;
            char msg[100];
            snprintf(msg, sizeof(msg), "This is a UART DMA transmission testing number %d.", testcount++);
            LOG("Writing %s", msg);
            /*
            bool status = false;
            me->m_uartOutFifo.WriteNoCrit((uint8_t *)msg, strlen(msg), &status);
            Evt *evt = new Evt(UART_OUT_WRITE_REQ, UART2_OUT, GET_HSMN());
            Fw::Post(evt);
            */
            return Q_HANDLED();
        }
        case UART_ACT_START_CFM: {
            EVENT(e);
            ErrorEvt const &cfm = ERROR_EVT_CAST(*e);
            if (me->GetHsm().MatchOutSeq(cfm)) {
                if (cfm.GetError() == ERROR_SUCCESS) {
                    if(me->GetHsm().IsOutSeqAllCleared()) {
                        LOG("UARTs started successfully");
                        Log::AddInterface(UART2_OUT, &me->m_uart2OutFifo, UART_OUT_WRITE_REQ);

                        //me->m_testTimer.Start(2000, Timer::PERIODIC);

                        Evt *evt = new SampleStartReq(SAMPLE, SYSTEM, 0);
                        Fw::Post(evt);
                        evt = new BtnStartReq(SEL_BTN, GET_HSMN(), GEN_SEQ());
                        Fw::Post(evt);
                        evt = new LedStartReq(LED0, GET_HSMN(), GEN_SEQ());
                        Fw::Post(evt);
                        evt = new LedStartReq(LED1, GET_HSMN(), GEN_SEQ());
                        Fw::Post(evt);
                    }
                }
            }
            return Q_HANDLED();
        }
        case BTN_UP_IND: {
            EVENT(e);
            // Commented for testing.
            //Evt *evt = new LedOffReq(LED0, GET_HSMN(), GEN_SEQ());
            //Fw::Post(evt);
            return Q_HANDLED();
        }
        case BTN_DOWN_IND: {
            EVENT(e);

            // Test only - for ATWINC1500 startup sequence.
            /*
            static bool atwinc1500Startup = false;
            if (atwinc1500Startup == false) {
                atwinc1500Startup = true;
                HAL_Delay(100);
                HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_SET);
                HAL_Delay(100);
                HAL_GPIO_WritePin(GPIOE, GPIO_PIN_15, GPIO_PIN_SET);
            }
            */
            return Q_HANDLED();
        }
        case LED_ON_CFM:
        case LED_OFF_CFM: {
            EVENT(e);
            return Q_HANDLED();
        }
    }
    return Q_SUPER(&QHsm::top);
}

QState System::Idle(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_speed = 0;
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case BTN_DOWN_IND: {
            EVENT(e);
            me->m_speed = START_SPEED;
            me->SetSpeed(me->m_forward, me->m_speed);
            Evt *evt = new LedOnReq(LED0, GET_HSMN(), GEN_SEQ());
            Fw::Post(evt);
            evt = new LedOnReq(LED1, GET_HSMN(), GEN_SEQ());
            Fw::Post(evt);
            return Q_TRAN(&System::Accel);
        }
    }
    return Q_SUPER(&System::Root);;
}

QState System::Activated(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_INIT_SIG: {
            EVENT(e);
            return Q_TRAN(&System::Accel);
        }
        case BTN_DOWN_IND: {
            EVENT(e);
            return Q_TRAN(&System::ToIdle);
        }
    }
    return Q_SUPER(&System::Root);
}

QState System::Accel(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_speedTimer.Start(SPEED_INTERVAL_MS, Timer::PERIODIC);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_speedTimer.Stop();
            return Q_HANDLED();
        }
        case SPEED_TIMER: {
            if (me->m_speed < MAX_SPEED) {
                me->m_speed += SPEED_STEP;
                me->SetSpeed(me->m_forward, me->m_speed);
                return Q_HANDLED();
            } else {
                return Q_TRAN(&System::Const);
            }
        }
    }
    return Q_SUPER(&System::Activated);
}

QState System::Const(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_runTimer.Start(me->m_runTime);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_runTimer.Stop();
            return Q_HANDLED();
        }
        case RUN_TIMER: {
            EVENT(e);
            return Q_TRAN(&System::ToRest);
        }
    }
    return Q_SUPER(&System::Activated);
}

QState System::Decel(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_speedTimer.Start(SPEED_INTERVAL_MS, Timer::PERIODIC);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_speedTimer.Stop();
            return Q_HANDLED();
        }
        case SPEED_TIMER: {
            if (me->m_speed > 0) {
                if ((me->m_speed < STOP_SPEED) || (me->m_speed < SPEED_STEP)) {
                    me->m_speed = 0;
                } else {
                    me->m_speed -= SPEED_STEP;
                }
                me->SetSpeed(me->m_forward, me->m_speed);
                return Q_HANDLED();
            } else {
                me->m_forward = !me->m_forward;
                return Q_TRAN(&System::Rest);
            }
        }
    }
    return Q_SUPER(&System::Activated);
}


QState System::ToRest(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
    }
    return Q_SUPER(&System::Decel);
}

QState System::ToIdle(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            return Q_HANDLED();
        }
        case SPEED_TIMER: {
            if (me->m_speed == 0) {
                me->m_forward = !me->m_forward;
                Evt *evt = new LedOffReq(LED0, GET_HSMN(), GEN_SEQ());
                Fw::Post(evt);
                evt = new LedOffReq(LED1, GET_HSMN(), GEN_SEQ());
                Fw::Post(evt);
                return Q_TRAN(&System::Idle);
            }
            // Lets parent state handle it.
            break;
        }
    }
    return Q_SUPER(&System::Decel);
}

QState System::Rest(System * const me, QEvt const * const e) {
    switch (e->sig) {
        case Q_ENTRY_SIG: {
            EVENT(e);
            me->m_restTimer.Start(REST_TIME);
            return Q_HANDLED();
        }
        case Q_EXIT_SIG: {
            EVENT(e);
            me->m_restTimer.Stop();
            return Q_HANDLED();
        }
        case REST_TIMER: {
            return Q_TRAN(&System::Accel);
        }
    }
    return Q_SUPER(&System::Activated);
}

} // namespace APP
